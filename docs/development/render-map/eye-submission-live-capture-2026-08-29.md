# Accepted eye-submission live capture — 2026-08-29

## Purpose

This bounded capture validates the accepted OpenVR compositor boundary used for
eye ownership. It also demonstrates why absence from an unqualified loading
interval cannot be generalized to a loaded presentation interval.

## Runtime identity

- Skyrim VR: `1.4.15.0`, process `SkyrimVR.exe`, PID `37120`.
- CSX source commit: `b223489b6b2afb5441409b3e17e826ff964efb50`
  (`v3.19.0-pr5-6-gb223489b6-dirty`).
- CSX build ID:
  `d284c9c368afbb32c8b8ed576184d2c0b951ffeeae46ece3a00d3f19ef528c2e`.
- Built DLL SHA-256:
  `6BB91080C5E1568CFEFD64FC1EB02C060117D2254BE9EA3D9A8CA41150D48FBF`.
- Runtime route: verified Valve null HMD; the successful capture began only
  after DevBench positively reported `Main Menu` open.
- MO2 profile:
  `Codex Task - 20260829t025250z-obb-resource-join-be0ece42`.

Canonical build receipts:

- configure:
  `L:/Codex/logs/managed-process/20260829-045901.242-csx-render-system-map-configure-prebuilt/receipt.json`;
- build:
  `L:/Codex/logs/managed-process/20260829-045912.209-csx-render-system-map-build-CSmain-prebuilt/receipt.json`.

## Controlled selector

Both captures requested only `eye-submitted`. The controller resolved that to
`resource-observed` plus `eye-submitted`, so the submitted texture could be
declared once without admitting unrelated render traffic. Filtered calls were
counted before any event or identity capacity was consumed.

| Capture | Interval qualification | Attempted | Filtered | Retained | Result |
|---|---|---:|---:|---:|---|
| `capture-live-0000e5e6e5595b98-1` | loading/fader transition; accepted submission not yet established | 271,100 | 271,100 | 0 | Valid negative interval only; no selected boundary occurred. |
| `capture-live-0000e6053a381f3c-3` | `Main Menu` positively open before start | 279,472 | 278,270 | 1,201 | One texture declaration and two accepted eye submissions for each of 600 CPU frames. |

Neither capture dropped an event or an identity record. The successful capture
ended at its 600-frame bound and is complete rather than truncated.

## Exact accepted-submit join

The successful trace contains:

- 1 `resource-observed` declaration, `obs-resource-471-g2`;
- 600 left-eye and 600 right-eye `eye-submitted` events;
- CPU frames `11367` through `11966`;
- compositor cycles `7870` through `8469`, exactly one cycle per CPU frame;
- one submission thread, ID `24708`;
- `submitFlags=0` for both eyes.

Both eyes submit the same side-by-side `3024 x 1680` texture. It is a
single-sample texture with DXGI format `28`, default usage, shader-resource and
render-target bind flags (`40` decimal). The left submission selects
`u=[0.0,0.5]`, `v=[0.0,1.0]`; the right selects `u=[0.5,1.0]`, `v=[0.0,1.0]`.
Thus eye ownership is not inferred from alternating draws or viewport position:
it is observed at the compositor's accepted submission boundary and joined to
the shared texture plus exact per-eye bounds.

The first retained record declares the resource. Each following eye event
references `obs-resource-471-g2` with role `submitted-eye-texture`. This closes
the prior uncertainty about whether the null-HMD route reached the hook. It
does not retroactively assign an eye to earlier work: draw-to-eye attribution
still requires a proven resource-flow path into this submitted allocation.

## Dependency-complete producer-to-submit join

Three successively narrower follow-up captures located the submitted texture's
last observed write and retained its intervening resource flow:

| Capture | Requested boundary | Retained | Result |
|---|---|---:|---|
| `capture-live-0000e6888ccffbd8-4` | render-target binds plus accepted eye submissions, 60 frames | 1,970 | The submitted texture is an RTV and is rebound immediately before submission. |
| `capture-live-0000e69c2f3fe500-5` | draws plus accepted eye submissions, 1 frame | 412 | Three draws target the submitted allocation; the last is a two-instance indexed draw. |
| `capture-live-0000e6abf9861384-6` | draws, resource flow, resource versions, and accepted eye submissions, 1 frame | 647 | The final observed write and every intervening read/copy are retained with zero drops. |

In the dependency-complete capture, `obs-resource-171-g5` is the shared
submitted allocation and `obs-render-target-172-g5` is its RTV. Three draws
write it. The final observed write is event sequence `435`, context command
sequence `352`, a `draw-indexed-instanced` call with six indices and two
instances through `obs-target-binding-184-g5`. Its selected capture-local
shader objects are `obs-vertex-shader-187-g5` and
`obs-pixel-shader-188-g5`.

After that write, the allocation is consumed as an input by the draw at event
sequence `509` and is the source of copy operations at sequences `514`, `516`,
`519`, and `520`. Those operations read the allocation; none writes it. The
left eye accepts the allocation at sequence `517` and the right eye at
sequence `524`. No later write to the submitted allocation occurs before
either submission.

The reproducibly derived graph contains 605 nodes and 1,685 edges. It versions
the submitted allocation as `node-resource-0231` (content version 3), records
event `435` as that version's producer, and emits confirmed presentation edges
from the same version to both accepted eye events. It also emits confirmed read
edges for the intervening draw and copy-outs. The sole graph gap is
non-blocking: overlap is conservative at allocation granularity because exact
view-subresource coverage and the state returned by D3D11 hazard resolution
are not yet observed.

This closes the structural producer-to-submit boundary for this captured
frame. It does not yet name the semantic shader or render phase responsible
for the final write. The stage observations identify live D3D11 objects, but
these objects were created before capture start and therefore lack a joined
creation-time bytecode hash, cache path, or stable shader-manifest identity.

### Post-capture identity refinement

The capture exposed two implementation causes for that missing identity. D3D
shader creation was intercepted only when `Dump Shaders` was enabled, and
capture start cleared the capture-local stage IDs without re-declaring already
bound objects. The follow-up source change now:

- intercepts VS, PS, and CS creation independently of debug dumping;
- retains only bytecode size and SHA-256 for render-map provenance unless full
  shader dumping is explicitly enabled; and
- lazily declares inherited bound stages on the first captured draw or
  dispatch without advancing the observed D3D command sequence.

The focused inherited-state regression test and all render-map tests pass. The
canonical `CSmain` build also passes:

- configure receipt:
  `L:/Codex/logs/managed-process/20260829-054225.798-csx-render-system-map-configure-prebuilt/receipt.json`;
- build receipt:
  `L:/Codex/logs/managed-process/20260829-054236.954-csx-render-system-map-build-CSmain-prebuilt/receipt.json`;
- built DLL SHA-256:
  `8D849B61F9474A60481810E4787CEB66BFDC7BB95CFB3F165FC916B80DF053E5`.

### Live validation of inherited stage identity

The rebuilt DLL was then deployed into the same owned MO2 profile and exercised
through the verified Valve null-HMD route. DevBench positively reported
`Main Menu` before capture start. Capture
`capture-live-0000e84ffead01ec-1` retained one frame with:

- 465 events from 499 attempted calls;
- 33 intentionally filtered calls and one frame-boundary rejection;
- zero dropped events or identity observations;
- 39 stage-shader declarations, all 39 carrying a bytecode size and SHA-256;
- 64 draws and two accepted eye submissions; and
- a complete, non-truncated `frame-limit` result.

The shared submitted allocation is `obs-resource-135-g1`; its render-target
view is `obs-render-target-136-g1`. The final observed draw into that allocation
before submission is event sequence `228`, context command sequence `182`, a
six-index, two-instance `draw-indexed-instanced` through
`obs-target-binding-148-g1`. The draw joins to these inherited stage objects:

| Stage | Observation | Bytecode bytes | Bytecode SHA-256 |
|---|---|---:|---|
| vertex | `obs-vertex-shader-151-g1` | 5,220 | `0d26605c847407eefd513eda14498f2e7834187c7ebe0f4b59139228ef3c94c3` |
| pixel | `obs-pixel-shader-152-g1` | 23,880 | `59edd046541167da07698d5a3d4027f490d457fdffaa40e5200a212c039fbe58` |

The left and right eye submissions reference the same allocation at event
sequences `310` and `317`. Intervening copy operations read the submitted
allocation into other resources; no later write to it is observed before
either accepted submission. The derived graph contains 419 nodes and 1,050
edges. Its one gap is the same non-blocking allocation-wide hazard caveat as
the earlier dependency-complete capture.

This live result validates the source refinement: a capture beginning after
shader creation can now recover stable bytecode identities for already-bound
VS/PS objects even when full shader dumping is disabled. It closes the
creation-provenance gap but does not, by itself, assign Skyrim/CSX semantic
names to those hashes.

### Live validation of engine-loader aliases

The next source refinement records a bounded, one-to-many engine alias set when
`BSShader_LoadShaders` associates a loaded D3D stage object with a Skyrim
`BSShader::Type` and descriptor. It deliberately keeps the D3D object and
bytecode hash as the primary identity: different loader entries can legally
reuse the same stage object or bytecode.

The canonical `CSmain` build passed with:

- configure receipt:
  `L:/Codex/logs/managed-process/20260829-060928.763-csx-render-system-map-configure-prebuilt/receipt.json`;
- build receipt:
  `L:/Codex/logs/managed-process/20260829-060939.750-csx-render-system-map-build-CSmain-prebuilt/receipt.json`;
- built DLL SHA-256:
  `942C50DC884B3A6130F15A7FDE2D5053FE101F68D9A167B6D2ACE8C0ABA2506A`.

DevBench reported render-map contract `1.8`, schema revision `9`, and `Main
Menu` open before capture `capture-live-0000e9b15db36444-1` began. The
one-frame capture retained 710 of 748 attempted observations: 37 calls were
intentionally filtered and one was rejected at the frame boundary. It retained
44 stage-shader declarations, 115 draws, 98 resource-flow events, and two
accepted eye submissions without dropping or truncating any identity catalog.

Ten stage observations carry an exact engine alias. The five observed
vertex/pixel pairs are:

| Loader type | Descriptor | Vertex bytecode SHA-256 | Pixel bytecode SHA-256 |
|---|---:|---|---|
| `ISCopy` | 0 | `6a98eaf84e45d6caff2dea483e2d4f588b3dcdc4ed71afe674f90d6130437a47` | `ee54b2c7c1924a05a1f859f8fd77161d428c3cdccc2a2b47fbb5cd789cc6ae61` |
| `ISSimpleColor` | 0 | `cf65ebf984f76f555a0f020dd734ff4a6e2b08241d729f410d6b2ac30675f562` | `92837895db528365acfde133503500ff49761bc2ec1854dbfa6e810ce66cba2f` |
| `ISBrightPassBlur15` | 0 | `6a98eaf84e45d6caff2dea483e2d4f588b3dcdc4ed71afe674f90d6130437a47` | `101a41414554a1bf106a7ec4d2fb7c0d6485bd269013455015afd25b994d07c1` |
| `ISBlur15` | 0 | `6a98eaf84e45d6caff2dea483e2d4f588b3dcdc4ed71afe674f90d6130437a47` | `ca4869ffe8d269be5463fab1e994d5cb849d2b42efbf8a1f727311de204e05a2` |
| `ISTemporalAA_Water` | 0 | `6a98eaf84e45d6caff2dea483e2d4f588b3dcdc4ed71afe674f90d6130437a47` | `4e170d17b70c22894756d9790696a4a837952d54d7ddf9b89262a1e3b3dde113` |

This is a positive live proof of the loader-to-D3D-object-to-bytecode join. It
also demonstrates why the relationship must be one-to-many: the identical
352-byte vertex shader is reused by four distinct image-space loader types.

The accepted eye textures are `obs-resource-209-g1` and
`obs-resource-215-g1`. Both are copied from `obs-resource-6-g1`, whose RTV is
`obs-render-target-186-g1`. The last observed write to that source before the
copies and submissions is event sequence `465`, context command sequence
`382`, a six-index, two-instance `draw-indexed-instanced` through
`obs-target-binding-198-g1`. Its shaders are the same final-write hashes found
in the preceding capture:

| Stage | Observation | Bytecode bytes | Bytecode SHA-256 | Engine alias |
|---|---|---:|---|---|
| vertex | `obs-vertex-shader-201-g1` | 5,220 | `0d26605c847407eefd513eda14498f2e7834187c7ebe0f4b59139228ef3c94c3` | none observed |
| pixel | `obs-pixel-shader-202-g1` | 23,880 | `59edd046541167da07698d5a3d4027f490d457fdffaa40e5200a212c039fbe58` | none observed |

The absence is now qualified rather than general: loader aliases are present
elsewhere in the same bounded frame, but the final stereo/compositor objects
did not retain one. The capture alone cannot distinguish a different creation
route from a replacement that occurred after alias registration, and its
semantic identity must not be guessed from bytecode size or frame position.

#### Managed-pack correlation and corrected diagnosis

A read-only scan of the managed shader packs found both final-stage bytecode
hashes in the `Effect` family. The 5,220-byte vertex hash is shared by cache
descriptors `0x42`, `0x442`, `0x4842`, `0x400042`, `0x800042`, and
`0x800442`. The 23,880-byte pixel hash is shared by descriptors `0x42`,
`0x72`, `0x152`, `0x800042`, and `0x800072`. Hash matching therefore proves
the family but cannot select the actual descriptor; bytecode reuse makes that
ambiguity intrinsic.

Source inspection then identified the missing join. The first implementation
registered engine aliases immediately after Skyrim's original shader load.
CSX's disk-cache path and loose-shader hook subsequently replaced entries in
the loader tables, leaving provenance attached to the displaced vanilla D3D
objects. Alias registration now occurs after both replacement paths complete,
so it describes the objects that draws can actually bind. A further generic
creation-callsite hook is not warranted unless live validation of this corrected
ordering still leaves a shader unexplained.

The corrected ordering passes the focused render-map tests, schema contracts,
graph-builder regression suite, and canonical `CSmain` build:

- configure receipt:
  `L:/Codex/logs/managed-process/20260829-063039.085-csx-render-system-map-configure-prebuilt/receipt.json`;
- build receipt:
  `L:/Codex/logs/managed-process/20260829-063057.744-csx-render-system-map-build-CSmain-prebuilt/receipt.json`;
- built DLL SHA-256:
  `659107DDEF839E674B74BC59A6B6F85DB57F21D209421F43FA999C4DB9CD4C76`.

The derived graph contains 656 nodes and 1,832 edges, no ambiguities, and five
non-blocking gaps. Four gaps are early draws whose output-merger state predates
capture; the fifth is the existing allocation-wide subresource-overlap caveat.

### Final selected-stage semantic join

A diagnostic capture that included `technique-resolved` events proved that the
final producer selects `Effect` descriptor `66` (`0x42`) for both stages. It
also exposed two capture-state defects rather than a missing engine relation:

- enriching an already observed D3D object with its wrapper, cache path, and
  engine alias was incorrectly treated as pointer reuse and advanced the
  capture-local generation; and
- the temporary stage-selection context was enabled only when the optional
  technique scope event itself was selected.

The collector now merges compatible, monotonically richer stage evidence into
the original observation. Conflicting wrapper, bytecode, or cache evidence
still advances the pointer generation. The `BSShader::BeginTechnique` hook now
keeps its stage-selection context for every active render-map capture while
leaving technique events subject to the caller's event filter.

The canonical build containing both corrections passed with:

- configure receipt:
  `L:/Codex/logs/managed-process/20260829-072502.589-csx-render-system-map-configure-prebuilt/receipt.json`;
- build receipt:
  `L:/Codex/logs/managed-process/20260829-072514.466-csx-render-system-map-build-CSmain-prebuilt/receipt.json`;
- built DLL SHA-256:
  `1161002154A88872D71A79E5490B9C28A78623D5CC69D865A5E461DA5D9CBAD8`.

Capture `capture-live-0000edcb824d3ac8-1` then requested only `draw`,
`resource-flow`, and `eye-submitted`. Technique events were neither requested
nor present. Its 647 retained events describe one complete frame with zero
drops, zero identity loss, and no truncation. The graph independently traces
both accepted eye textures to `draw-indexed-instanced` event `435`, context
command sequence `352`. The draw's bound stages are:

| Stage | Observation | Generation | Wrapper descriptor | Cache path | Bytecode bytes | Bytecode SHA-256 | Engine alias |
|---|---|---:|---:|---|---:|---|---|
| vertex | `obs-vertex-shader-187-g1` | 1 | 66 | `Data/ShaderCache/Effect/42.vso` | 5,220 | `0d26605c847407eefd513eda14498f2e7834187c7ebe0f4b59139228ef3c94c3` | `Effect:66` |
| pixel | `obs-pixel-shader-188-g1` | 1 | 66 | `Data/ShaderCache/Effect/42.pso` | 23,880 | `59edd046541167da07698d5a3d4027f490d457fdffaa40e5200a212c039fbe58` | `Effect:66` |

All 41 stage observations in the capture remain generation 1. This is the
ordinary-capture proof: semantic selection, wrapper identity, managed-cache
path, bytecode identity, final producer draw, and accepted compositor
submission are joined without enabling diagnostic technique output.

### Static compile and engine-map join

The graph builder previously accepted a shader manifest and engine map only as
hashed provenance inputs. It now consumes their identities. For every exact
runtime loader alias, it creates a correlated `implemented-by` edge to the
unique engine shader-cache compile unit. At a draw, it also combines the bound
vertex and pixel aliases and resolves the pair to a unique versioned engine-map
technique. Candidate multiplicity creates an ambiguity rather than a guessed
edge.

Re-deriving this capture with the source-revision shader manifest and Skyrim VR
1.4.15 engine map produced:

| Result | Value |
|---|---:|
| nodes | 614 |
| edges | 1,714 |
| compile-unit nodes | 5 |
| engine-map technique nodes | 4 |
| runtime-stage to compile-unit edges | 17 |
| ambiguities | 0 |
| non-blocking gaps | 1 |

For final draw event `435`, the exact runtime chain is now machine-readable:

```text
engine-technique-0004 (Effect 0x42 / 0x42)
  -> obs-vertex-shader-187-g1
       -> compile-0074 (Effect.hlsl)
  -> obs-pixel-shader-188-g1
       -> compile-0074 (Effect.hlsl)
  -> draw event 435
  -> final resource-flow producer
  -> accepted left and right eye submissions
```

The descriptor decoder proves the engine-controlled base defines
`TEXCOORD | TEXTURE`. Compilation additionally supplies `VR` and the applicable
stage macro (`VSHADER` or `PSHADER`). `compile-0074` contributes a 24-file
structural dependency closure including `Effect.hlsl`; this is deliberately a
conservative conditional closure. The exact resident CSX feature-define set was
not present in this capture, so the bytecode hashes are exact artifact identity
but the active preprocessed include closure cannot yet be reconstructed from
source alone.

## Retained evidence

Evidence root:

`L:/Codex/evidence/render-map/20260829-eye-submission-live-join/`

Hashes:

| Capture artifact | SHA-256 |
|---|---|
| empty interval manifest | `551C9E92B0C8E3EB2D8C2C8D68B9024F48F926EEC3843E8871152BB0F1802E2C` |
| empty interval events | `E3B0C44298FC1C149AFBF4C8996FB92427AE41E4649B934CA495991B7852B855` |
| main-menu manifest | `097E210FB24A5D7AEC1AF3B9452EC2AB4C89982083DF401E15B2C2F8415730A0` |
| main-menu events | `98D37512949F2EECA9A11A5B1A9AF62BA3EED2E9FA8CEF4E911C579F96AE3A11` |
| target/submit manifest | `3B7E05946A09C3198A080FE4900EA7368EDDEE6ED0F6B4CCAF2D45D21997A44E` |
| target/submit events | `1A4594B0C83576D3886416FDF0E10DF6966A47065C3A3D4E08307FE3B842D22D` |
| draw/submit manifest | `65B14AD28ACE324EE484D7416E3DE6A8CDAE02B82ED72D8DCB128971D50C7A03` |
| draw/submit events | `F239D9295F6FA8A80D9A054ABA4C7934901F7C9990CC7E0C4AA02ECB177664AC` |
| dependency-complete manifest | `5BEE573CCF1615A8993FACB37E77E7EA8CCFC317B95B2EA89082A03E57E16005` |
| dependency-complete events | `5491740C21014BADB36354582F4A0438BECAF2365A67030557882522D4F645C7` |
| derived render graph | `BE85E9FBEAFF6307AEE8BA4096005AD0CB506C35B809B93D59A4DF40956ED0BE` |

Inherited-stage validation evidence root:

`L:/Codex/evidence/render-map/20260829-stage-provenance-live/`

| Artifact | SHA-256 |
|---|---|
| capture manifest | `13FF47DC73A774AA994AA7709CC6B9AE66D06D22DC3DEFE58F11690E73DDA70A` |
| events | `1CA5D6B286CC7661E54E53A3C16B4D213C594B4E91BC538E64199C195A105344` |
| derived render graph | `972038005018EC95182E1B3D41A6D40FFA1941F4402D815CEE86A05ED526B60C` |

Engine-alias validation evidence root:

`L:/Codex/evidence/render-map/20260829-engine-alias-live/`

| Artifact | SHA-256 |
|---|---|
| capture manifest | `E6CC7F33EF2F506F23683664B9B285B4C24F743A4C06D2B5EFE4E9F8478925CB` |
| events | `EA4F7CBDB32A28B5270407BE7FB45ABA4DF66B806BA414257477E1286305983B` |
| derived render graph | `7B663EF2C44E590C5CA8D380BC9E1BFAD92221BFFAAA55A348F31241896126D0` |

Final ordinary semantic-state evidence root:

`L:/Codex/evidence/render-map/20260829-semantic-state-final-live/`

| Artifact | SHA-256 |
|---|---|
| capture manifest | `278F111ADAC5341D25197E34904C708162F9BE5CB859A7A9618DDA526A726BE9` |
| events | `057599DC1654B946EDAAA5501169CCFBFFA988251AB8889FB3501BA09864DA3F` |
| derived render graph | `64E4C3730C9BF6786F9C502CDD72DDE69CE4797FE705D31AC3BC3A5ACA283378` |
| statically joined render graph | `A5BF3FC58FD303FB0B30CCBF9FEAB55ADD2FEAF01DF50AA8CA1E4490758EB822` |

## Remaining boundary

The resource-flow path from the final observed write to both accepted eye
submissions is complete for the retained frame. Inherited VS/PS bytecode,
selected wrapper descriptors, managed-cache paths, and the exact `Effect:66`
semantic alias are live-validated on the same observations used by the final
draw. The former callsite-provenance gap is therefore closed without a generic
creation-callsite hook. Exact subresource and pixel coverage, additional D3D11
stages and deferred-context command lists, semantic projection of more render
phases, and validation on the physical SteamVR route remain separate
refinements.
