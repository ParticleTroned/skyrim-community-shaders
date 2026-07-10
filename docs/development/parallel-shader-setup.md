# Parallel `SetupGeometry` / `SetupMaterial` (multithreaded render-pass setup)

## Goal

The Skyrim render thread walks its render-pass lists **serially**, and for every
pass calls `BSShader::SetupMaterial` (bind textures + material constants) and
`BSShader::SetupGeometry` (compute per-object transforms/lights/material
projection into a constant buffer + track dirty state) before the draw. On a
CPU-bound frame this loop is the render thread's dominant cost. The goal is to
parallelize the **CPU-side setup** of `BSLightingShader` and `BSUtilityShader`
passes across worker threads, backed by **per-thread render state**, so the
single render thread stops being the bottleneck.

## Why it is single-threaded today (RE findings, SE 1.5.97)

The setup flow (from IDA + Nukem's SkyrimSETest reimplementation):

```
BSBatchRenderer::RenderBatches / RenderPersistentPassList   // walk BSRenderPass list
  -> RenderPassImmediately(pass, technique, alphaTest, flags)
       if technique changed:  BeginPass(shader, technique)        // bind VS/PS technique
       if material changed:   shader->SetupMaterial(material)     // vfunc 0x20 (idx 4)
       -> RenderPassImmediately_Standard/Skinned/Custom
            ShaderSetup: shader->SetupGeometry(pass, flags)       // vfunc 0x30 (idx 6)
            Draw(pass)                                            // Map dyn VB + DrawIndexed
            shader->RestoreGeometry(pass, flags)
```

Every step mutates **process-global** state at fixed absolute addresses (SE):

| Global (SE addr)             | Meaning                                                        |
|------------------------------|----------------------------------------------------------------|
| `RendererShadowState` @ `0x143027EB0` (`RELOCATION_ID(524773)`) | 0x5E0-byte struct: `stateUpdateFlags`, PS/CS texture+sampler *modified-bit* masks, bound `PSTexture[16]`, depth/alpha/raster modes, `currentVertexShader`/`currentPixelShader`, topology, `posAdjust`/`cameraData`. This is the "shadow" of the GPU state used to skip redundant sets. |
| `ConstantBuffers` @ `0x1430281F8` | Inline array of CB descriptors `{resource, mappedPtr, ...}` mapped per draw. |
| `LocalStorage` (context) @ `0x143027EA0` | The renderer/immediate-context wrapper (`Map`=vtbl+0x70, `Unmap`=vtbl+0x78). |
| `pCurrentVertexShader` @ `0x143028200`, transform globals `0x14302820C`/`0x143028210` | Current bound VS + eye/pos-adjust. |
| `LastPass`/`LastShader`/`LastMaterial` (Renderer+0x3014/0x3018/0x3500) | Redundancy filters that let consecutive passes skip `BeginPass`/`SetupMaterial`. |

`SetupGeometry` (BSLighting, `0x1412F2BB0`, ~3.2 KB) does, per pass:
1. `context->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, ...)` the geometry CB(s),
2. writes transforms (`GeometrySetupConstantWorld`), directional + point lights,
   material projection, alpha, fade, etc. into the mapped memory,
3. flips **modified bits / dirty flags** in `RendererShadowState`,
4. `context->Unmap` + binds the CB slice.

Because `RendererShadowState`, `ConstantBuffers`, and the redundancy filters are
**inline globals baked into the machine code as absolute addresses**, two threads
running the engine's `SetupGeometry` concurrently corrupt each other's mapped CB,
dirty flags, and modified-bit masks. The context pointer is swappable, but the
inline state is not — so there is **no pointer to swap per-thread**. This is why
the goal requires per-thread state (TLS): the only correct way to parallelize is
to run setup code that touches **per-thread** state instead of these globals.

## Ordering constraints

- Group index 0/2 (opaque, no alpha test) passes are order-independent → freely
  parallelizable, results merged in any order.
- Group index 1/3/4 (alpha-tested / alpha-blended) are order-dependent → their
  *draws* must replay in the original list order. Setup (CB compute) can still be
  parallel; only submission order is constrained.
- `Draw` and all D3D immediate-context calls must not run concurrently on the
  immediate context (D3D11 immediate context is single-threaded; DXVK's too).

## Architecture (revised: per-pass constant buffers, not per-thread state)

The dominant per-pass cost is not the draw — it is the `context->Map(geometryCB,
D3D11_MAP_WRITE_DISCARD)` at the top of `SetupGeometry`. `SetupGeometry`'s tail
shows the mechanism (SE): `LocalStorage_143027EA0` is the immediate
`ID3D11DeviceContext` (vtbl **+112 Map**, **+120 Unmap**, **+56
VSSetConstantBuffers slot 2**, **+128 PSSetConstantBuffers slot 2**). Every
BSLighting/BSUtility pass DISCARD-maps the *shared* geometry CB, writes its
constants, unmaps, and binds it at VS/PS slot 2. ~21k of these maps/frame were the
render thread's hottest cluster (DXVK CB allocator popSlot/refill).

So instead of parallelizing the whole setup with per-thread render state (deferred
contexts + `RendererShadowState` copies — heavy and risky), **give each pass its
own constant buffer region and fill them in parallel; the serial render binds the
pre-filled region instead of doing the expensive Map/write.** This drops the two
hardest pieces entirely — no per-thread shadow state, no deferred contexts, draws
stay serial.

Concretely (D3D11.1 offset binding):
1. **One big dynamic constant buffer per frame**, `D3D11_USAGE_DYNAMIC` +
   `BIND_CONSTANT_BUFFER`, sized `maxPasses * sliceBytes`, mapped **once** per
   frame `WRITE_DISCARD` (or ring `NO_OVERWRITE`); its CPU pointer is shared.
2. **Each pass = a 256-byte-aligned slice** (offset). Worker threads fill their
   passes' slices in parallel — plain CPU writes into disjoint offsets, **no
   `Map` calls on any thread**.
3. The fill is a **pure reimplementation of `SetupGeometry`'s CB math** — world +
   previous-world transpose (`sub_1412F4430` → `mappedCB[offset] =
   transpose(transform)`), directional/point/ambient lights, material projection,
   alpha/fade — writing at offsets read from **that pass's own shader constant
   table** (`VertexShader::constantTable` / `PixelShader::constantTable`), not the
   global per-shader offset cache (`ConstantBuffers+80`), so passes of different
   techniques fill correctly in parallel.
4. The serial render hooks `SetupGeometry` to **skip Map/write** and bind the
   pass's slice via `VSSetConstantBuffers1(2, bigCB, sliceOffset/16,
   sliceSize/16)` (+ PS). The non-CB side effects (dirty flags, texture binds)
   still run serially.

This means the only reimplementation is the **CB-content math as a pure per-pass
function** — no state machine, no draw, no deferred contexts.

Flow:
```
before the serial batch walk:
  bigCB = MapOnce(frame)                        // 1 map/frame, not 21k
  collect BSLighting/BSUtility passes, assign each a slice offset
  parallel_for pass in passes:                  // pure CPU fills, disjoint slices
     FillGeometryConstants(pass, camera, bigCB + pass.sliceOffset)
  UnmapOnce()
serial batch walk (engine, unchanged ordering):
  SetupGeometry hook for a filled pass:
     bind bigCB slice at VS/PS slot 2 by offset  // no Map/DISCARD/write
     run the pass's non-CB state (textures, dirty flags)
```

## The interleaving constraint (why the fill must be a pre-pass)

The engine walks passes once, interleaving `SetupGeometry` and `Draw`. A constant
buffer cannot be bound for a draw while it is mapped, so "map the arena once, bind
slices by offset" only works if **every** slice is written before **any** draw —
i.e. the fill must be a separate phase that runs before the draw walk:

```
Phase A (parallel pre-pass): arena mapped -> fill every pass's slice -> unmap
Phase B (serial draw walk):  SetupGeometry hook binds pass's slice by offset;
                              its non-CB side effects still run here
```

This is why the fill has to be reimplemented as a pure per-pass function: the
engine's own `SetupGeometry` can't run in Phase A (it mutates the global
`RendererShadowState` at fixed addresses and would race), and it can't be the
thing that fills the arena in Phase B either (that's the serial cost we're
removing).

`BSUtilityShader::SetupGeometry` (`0x14130EC70`) decompiled: the **CB writes** are
world-matrix transpose at `constantTable[world]`, shadow-light params
(`SetupShadowLightParameters`, shadow distance/falloff/projected-bounds), fade,
wind/tree params, and alpha-test ref — all pure functions of the pass + camera +
that pass's shader constant table, so they move to Phase A. The **global
shadow-state mutations** (`stateUpdateFlags`/PSResource/PSSampler modified bits for
the blockout/effect texture + depth mode at `0x143027EB0/EB4/EB8`) are pipeline
state, not CB contents, so they stay in Phase B (the serial walk, unchanged).

## Phased plan

- **P0 (landed, being reshaped): worker pool + off-by-default plumbing.** The
  deferred-context / per-thread-`RendererShadowState` shape from the first P0
  commit is replaced by the per-pass CB-slice buffer below; the thread pool stays.
- **P1: prove the mechanism on BSUtility (simplest constants).** Replace the
  shared geometry CB with a per-pass slice, fill it **serially** first, bind by
  offset. Validate pixel-identical shadow/depth. Then move the fill to workers.
- **P2: BSLighting.** Port the full CB-content math (world/prev-world, directional
  + point + ambient lights, material projection, alpha/fade) from Nukem
  `BSLightingShader.cpp` + the IDA transcription, keyed off each pass's shader
  constant table.
- **P3: tune worker count / slice sizing; extend to more shaders if it pays off.**

## BSUtility implementation recipe (concrete)

Everything needed to build the BSUtility path:

- **Collection seam / hook:** `BSShaderAccumulator::RenderGeometryGroup`
  (`0x1412CCE40`, `RELOCATION_ID(99963, …)`). At entry the `BSBatchRenderer` for
  the group already holds every accumulated pass in `m_RenderPass` (a
  `BSTArray<PassGroup>`, each `PassGroup{ BSRenderPass* m_Passes[5]; uint32_t
  m_ValidPassBits }`, chained via `BSRenderPass::m_PassGroupNext`). Walk it to
  collect this group's passes.
- **Pass filter:** a pass is BSUtility iff `pass->shader == BSUtilityShader::
  GetSingleton()` (`RELOCATION_ID(528354, 415300)`).
- **Fill (Phase A, parallel):** for each collected BSUtility pass, write into its
  arena slice (from `AllocateSlice`) exactly the constants
  `BSUtilityShader::SetupGeometry` writes — world matrix transpose at
  `constantTable[world]`, shadow-light params (only for the `0x1E00000` technique
  bits), fade / wind / tree, alpha-test ref — read from `pass`, the camera
  (`RendererShadowState` cameraData), and *that pass's* VS/PS `constantTable` for
  the offsets. Pure per-pass, no globals → dispatch across the worker pool.
- **Bind (Phase B, serial):** hook `BSUtilityShader::SetupGeometry` (vtbl idx 6,
  `VTABLE_BSUtilityShader[0]`). For a pass that was pre-filled, bind its slice at
  VS/PS slot 2 via `ID3D11DeviceContext1::VSSetConstantBuffers1(2, &arena,
  &firstConstant=sliceOffset/16, &numConstants=sliceBytes/16)` (+ PS) and run only
  the pass's global `RendererShadowState` side effects (dirty flags, texture
  binds) — skip the `Map(DISCARD)`/write. Passes not pre-filled fall back to the
  original.

Incremental, each step in-game screenshot-validated before the next:
1. Hook `RenderGeometryGroup`; walk + count BSUtility passes; parallel no-op fill;
   call original unchanged. Proves collection + threading + hook fire with **zero**
   visual change (log pass counts, confirm no crash / identical frame).
2. Add the real per-pass fill + the `SetupGeometry` bind hook. Confirm
   pixel-identical vs off (a fill/offset bug corrupts geometry grossly, so a
   screenshot A/B + moving around is a sufficient validator here).
3. Get user validation, then apply the same pattern to BSLighting.

## Validation (hard requirement — learned from the draw-cull revert)

Rendering changes cannot be validated from still screenshots + FPS. Any pass that
routes through the parallel path must be proven **pixel-identical** to the serial
path (RenderDoc frame A/B on the affected passes) and exercised **in motion**
across interior + exterior before it is enabled by default. The feature ships
**OFF** until that bar is met. Never deploy an unvalidated build to the game.

## Key references

- Nukem SkyrimSETest: `BSShader/Shaders/BSLightingShader.cpp`,
  `BSBatchRenderer.cpp`, `BSGraphics/BSGraphicsRenderer.cpp` — the cleanest
  reimplementation, already routed through `GetRendererShadowState()` /
  `GetShaderConstantGroup()` and TLS technique state.
- CommonLibSSE-NG `RE/R/RendererShadowState.h` — exact per-thread state layout.
- IDA: `BSLightingShader::SetupGeometry` `0x1412F2BB0`, `SetupMaterial`
  `0x1412F2020`; `BSUtilityShader::SetupGeometry` `0x14130EC70`.
