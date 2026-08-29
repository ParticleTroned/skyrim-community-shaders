# Material texture dependency — Breezehome live capture — 2026-08-29

## Result

The material-texture dependency slice passed its first bounded live world-scene
gate for both Lighting and Effect. Each observed runtime texture binding joins
an exact material-state revision to an earlier capture-local D3D11 resource
declaration. The derived graphs contain the expected `binds-texture` and
`resolves-to-resource` edges, with no blocking gap, event loss, catalogue
overflow, or binding/path truncation.

This closes the first concrete input edge below the Skyrim material state:

`scene object -> geometry -> material revision -> runtime texture binding -> D3D11 resource`

The observed binding index remains a runtime material-list position. The
capture does not claim an HLSL register or D3D11 SRV slot.

## Runtime provenance

| Item | Exact value |
|---|---|
| Lighting capture | `capture-live-0000fa821210da94-4` |
| Effect capture | `capture-live-0000fa88d3fb4a58-5` |
| Skyrim runtime | `SkyrimVR.exe` 1.4.15 |
| CSX source commit | `b223489b6b2afb5441409b3e17e826ff964efb50` (dirty working tree) |
| deployed DLL SHA-256 | `7A12DFE74E20EA64911AF6FEC57561B44681267548F0B4B3C5B8F3EE9485C879` |
| DevBench build ID | `74d8a0df9199e7eeddfe78849af0cac470b8e2e52db773b23deb327cc0d7b7ce` |
| fixture | verified Breezehome interior save, `Save2_2FAD4605_0_507269736F6E6572_WhiterunBreezehome_000005_20260822173026_1_1` |
| VR route | Valve null HMD plus qualified Codex head-pose driver |
| shader selectors | engine shader type `6` (Lighting) and `7` (Effect) |
| shader-cache ABI | `85f71f8f713542badba1027e6216e07f42a762782165d185f004e4c14af1d5c6` |
| shader compiler | `d3dcompiler_47.dll:10.0.26100.9168` |

The managed shader pack was reused with `contracts-match`; this run did not
compile shaders. Exact MO2 workspace, fixture, deployed-artifact and null-HMD
identity are retained by the harness receipts rather than inferred from the
game image.

## Bounds and completion

Each final capture selected object, geometry, material, resource and balanced
geometry-setup events, with `executionWithinSelectedGeometry: true`. The
catalogue budget was deliberately biased toward the enlarged material records:
one frame, two seconds, 8,192 events, 32 MiB, 1,024 materials, 1,024
geometries, 1,024 resources and 512 scene objects. Completion was
`frame-limit` in both cases.

| Measure | Lighting | Effect |
|---|---:|---:|
| accepted events | 3,741 | 321 |
| scene objects | 255 | 33 |
| geometries | 726 | 68 |
| material states | 726 | 68 |
| D3D11 resources declared | 582 | 16 |
| dropped events | 0 | 0 |
| dropped catalogue entries | 0 | 0 |
| scope overflow / mismatch | 0 / 0 | 0 / 0 |
| truncated | false | false |

One event arriving after each exact frame boundary was rejected and reported
as a boundary rejection. It is outside the accepted window and is not evidence
loss. High filtered-event counts are intentional shader-family and event-kind
selection, not drops.

## Lighting findings

The Lighting frame contains 4,922 texture bindings across 726 material states:

- every binding has a non-empty path and a non-null resource observation ID;
- all 4,922 resource joins resolve to an earlier declaration;
- no material binding list or path is truncated;
- all bindings use the explicit `runtime-material-list` role;
- material lists contain two through eight bindings, with 639 of 726 materials
  exposing seven;
- 580 distinct paths map one-to-one to 580 referenced D3D11 resources and 580
  `NiSourceTexture` identities in this capture; and
- all material observations remain at revision 1, with no same-window state
  mutation.

The 4,922 bindings include shared fallback allocations such as
`DefaultWhiteMap` and `BSShader_DefHeightMap`, plus concrete loose/archive
paths. Repeated use of one path/resource by many materials remains many exact
material-binding edges, not many resource identities.

The derived graph contains 7,936 nodes and 12,022 edges, including exactly
4,922 `binds-texture` and 4,922 `resolves-to-resource` edges.

## Effect findings

All 68 Effect material states expose two bindings:

- index 0 is `effect-source`;
- index 1 is `effect-greyscale`;
- all 136 paths and resource IDs are present and resolve backward in the event
  stream;
- 14 distinct paths map one-to-one to 14 referenced resources; and
- all materials remain at revision 1.

The greyscale role resolves either the shared `BSShader_DefNormalMap` fallback
or the observed `textures\\effects\\gradients\\GradAmbFog01.dds` allocation.
The source role retains the actual effect texture, including dust, candle,
smoke, glow and caustic assets. The graph contains 136 `binds-texture` and 136
`resolves-to-resource` edges.

## Graph qualification

Both graphs are acyclic and have no blocking gap. Their single non-blocking
gap is the pre-existing allocation-wide D3D11 hazard qualification: exact
view-subresource overlap and the state returned by hazard resolution are not
yet observed. It does not weaken these exact capture-local material-to-resource
joins.

Two declared resources in each stream are not referenced by a retained
material binding. The graph intentionally creates resource nodes only when an
evidence-bearing relation uses them; it does not invent edges for declarations
that are merely present.

## Controller finding

The live gate exposed a default-budget regression. Material records now retain
up to 32 bounded 383-byte texture paths. The service still defaulted to 4,096
material records plus the earlier large catalogue defaults, while advertising
a hard 32 MiB maximum. Consequently a caller that omitted explicit catalogue
bounds could receive `invalid_bounds` from an otherwise valid `start` request.

The controller defaults are corrected after this capture to form a useful
32 MiB-admissible profile, `maxBytes` defaults to the advertised ceiling, and
the registry reports the exact default profile and fixed-catalogue byte cost.
Impossible custom budgets now report `fixedCatalogueBytes` and
`minimumMaxBytes`. Callers can still raise one catalogue while lowering others
for a targeted question, as this live run did.

This compatible controller correction advances the render-map service to
contract `1.12`, schema revision `13`. It was made after the live capture and
therefore has build-and-contract-test evidence, not a claim of separate live
runtime validation.

## Build and teardown qualification

The post-capture source, including the controller-budget correction, passed:

- `tools/test-render-map-contracts.ps1`;
- `tests/render_graph_builder_test.py`; and
- the compiled `RenderMapCollector`, `RenderMapRuntime`,
  `RenderMapController`, and `RenderGraphBuilder` CTest targets.

The canonical `CSmain` build completed successfully. Its exact artifact is
`build/ALL-Prebuilt/Release/CommunityShaders.dll`, SHA-256
`3FA6CDF3002E06367F6241C9966C657DDB75AFD627025CEBD15D8E76982A3BC4`.
The configure and build receipts are retained at:

- `L:/Codex/logs/managed-process/20260829-113833.097-csx-render-system-map-configure-prebuilt/receipt.json`; and
- `L:/Codex/logs/managed-process/20260829-113845.702-csx-render-system-map-build-CSmain-prebuilt/receipt.json`.

The live working cache was classified `known-working` and preserved under
tree SHA-256
`0C216F2A65B3AEB74796B69E3522F082804D5C0928AF8F9FBBAE3993AB0B9789`.
The cache lifecycle then restored the exact pre-task tree
`7A8A9AA134AAF3F6A56F5E71E3A713DCAF372EE16E891F29957E523FFF4126F7`.
Skyrim and MO2 closed cleanly, RootBuilder had no active build transaction,
the task profile's OpenComposite marker returned to its exact original state,
and the MO2 lease was released. SteamVR stopped normally and its transaction
restored the original settings hash
`EE31AC1004DF9AA152264A600BDD43C87D31D8628E9CCA5B6075393B3E4D0A38`.

## Retained evidence

Authoritative directory:
`L:/Codex/evidence/render-map/20260829-material-texture-live`

| Artifact | SHA-256 |
|---|---|
| Lighting manifest | `2CE4C7A88BE18EC90A9B772F4FF4F28721900845698A6E27A01D954F99E6CAE6` |
| Lighting events | `2EB574E0BE0DDF80A85DEF731E1994C09F67106BE0228BD1B1A292643696EDE3` |
| Lighting graph | `F3C4333015C2E9614A6686D2ABCBFB6C3807E3CCC31AAE08BCFDB4E254347EA8` |
| Effect manifest | `49F319F875D27CD5CAE273822F00FDEACA5DE50073FA717A5F918CFDB3B283F6` |
| Effect events | `ED3A623A7C2978C74CB405CAC47BE3488F6BFBB6F7DB8E6A800364700EEAC0AA` |
| Effect graph | `61B804E44F746FCB168D23090278720011C14473AB030B41464ECDE3E13EEC9E` |

Large captures and derived graphs remain retained evidence rather than Git
payload. The evidence tree also contains the exact registry, runtime binding,
capture start/stop and scene-validation receipts.

## Next gate

The next material-input slice should join the runtime binding to the effective
D3D11 SRV slot and sampler state at execution. That will establish whether a
material-list position actually reaches a particular shader stage/register
without assuming a fixed mapping. Water remains a targeted scene-specific
follow-up for its static-reflection and four-normal roles.
