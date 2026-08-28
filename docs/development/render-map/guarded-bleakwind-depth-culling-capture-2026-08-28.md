# Guarded Bleakwind current-frame depth-culling capture (2026-08-28)

## Purpose

This run answers three narrow questions in a stable exterior scene:

1. Is the experimental path consuming the current frame's visibility result?
2. How much GPU work does the currently covered path save relative to an
   otherwise identical forced-visible control?
3. Does one visibility-gated draw cover one eye or both VR eyes?

The durable evidence root is:

`L:\Codex\projects\CSX-All-Development\evidence\20260828-vr-depth-culling-guarded-exterior`

## Provenance and scene guard

- CSX source commit: `015c3090d40f4ad99453db6b031bcc18e6a08286`
- CSX build ID:
  `6a7879a9a85ddbc70945bb53839d021123e44f0bd4490d8885e2f8e402ecd2db`
- Deployed DLL SHA-256:
  `BB4E2924675A1E5B664CA088AD4BE71E6E4FA90ABA6C75018A5C1D14B356E57F`
- Shader-cache ABI:
  `85f71f8f713542badba1027e6216e07f42a762782165d185f004e4c14af1d5c6`
- MO2 workspace:
  `20260827t194315z-depth-culling-covered-work-3b476764`
- Null-HMD state: qualified at standing eye height, with no input-driven pose
  movement and no qualification blockers.

Skyrim started at the main menu. DevBench loaded the workspace-authorized
Breezehome save, and the scene was positively identified before relocation.
`tgm`, `tdetect`, and `tai` were accepted onto Skyrim's main-thread queue before
one `coc BleakwindBasinExterior01`. The console output markers were not retained,
so those toggles are recorded as queued safeguards rather than independently
queried state. The stronger lifecycle checks were affirmative throughout the
measurement window:

- cell: `BleakwindBasinExterior01`;
- worldspace: `Tamriel`;
- position: `(3856.4426, 372.1646, -5743.1812)`;
- player health: `100 / 100` after relocation;
- no death, auto-load, second relocation, or blocking menu;
- the inspected cell and position remained unchanged after the paired samples.

DevBench and the test harness may add unmeasured latency. The comparison is
therefore intentionally like-for-like within one process, scene, build, shader
cache, pose, and instrumentation configuration. The absolute timings should not
be treated as an uninstrumented player benchmark.

## Controlled GPU-work comparison

`forced_visible` preserves the native OBB producer, the current visibility
resource, descriptor lookup, SRV binding, draw submissions, and shader branch.
It changes only the final visibility data bound to the shader, making every
covered object visible. The difference from `live` therefore estimates work
avoided by the visibility decision rather than work avoided by disabling the
producer or changing submission topology.

| Sample | Mode | GPU samples | Objects | Occlusion | Covered lighting draws/sample | Covered span (ms) | OBB-ready span (ms) |
|---|---:|---:|---:|---:|---:|---:|---:|
| A | live | 2105 | 2019 | 55.65% | 1675.74 | 21.2937 | 21.8932 |
| A | forced visible | 643 | 2019 | 55.63% | 1677.00 | 21.4884 | 22.0982 |
| B | live | 645 | 2022 | 55.74% | 1677.16 | 21.7069 | 22.3492 |
| B | forced visible | 639 | 2022 | 55.65% | 1680.69 | 21.9505 | 22.5743 |

The forced-visible minus live differences were:

| Pair | Covered-span saving | Relative | OBB-ready-span saving | Relative |
|---|---:|---:|---:|---:|
| A | 0.1947 ms | 0.91% | 0.2050 ms | 0.94% |
| B | 0.2435 ms | 1.12% | 0.2251 ms | 1.01% |

The repeat preserves the sign and approximate magnitude. In this exterior
scene, the demonstrated saving is therefore about `0.20-0.24 ms` per frame in
the instrumented coverage region. This is evidence of a modest current benefit,
not a claim that depth culling saves one percent of all GPU work in every scene.

Vertex- and pixel-shader pipeline-statistics invocation counts were nearly
unchanged between modes. That is expected for this implementation: the draw and
shader invocation still exist, and the occluded branch returns before the
normal vertex work and raster output. D3D11 pipeline statistics count the
invocation, not the amount of instruction work completed inside it.

Only Lighting draws were visibility-bound in these samples. The diagnostic
reported zero bound grass draws and zero bound distant-tree draws. The clean
render-map frame nevertheless contains 54 Grass geometry setups, so grass was
present and reached `BSGrassShader::SetupGeometry`; every one failed visible
before a visibility submission was declared. The same frame contains 2,059
Lighting geometry setups, of which 1,691 declared a visibility consumer, and no
DistantTree geometry setup. Consequently, the measured saving does not include
grass or distant-tree suppression even though the shader sources contain
guarded paths for those families.

## Same-frame and submission proof

The strict one-frame render-map capture is:

`render-map\capture-live-0000a9c30ce988f0-2`

Its `events.jsonl` SHA-256 is:

`4660F235F854EBB6BAF28C13229FC5EF7EF81EA116F5BC5A99B0F4DD206A8526`

The capture completed because of its one-frame bound with:

- 53,745 retained events;
- zero dropped events;
- no synthetic gap event;
- `truncated: false` and `events.complete: true`.

For CPU frame `30402` it records:

- one `visibility-result-ready` for 2,033 objects;
- resource version `obs-resource-version-4537-g2`;
- 1,691 `visibility-consumed` events;
- 1,691 explicit submission-to-draw matches;
- zero requested-versus-effective SRV binding mismatches;
- every consumer using that exact resource version in CPU frame `30402`;
- no forced-visible consumer in the structural capture.

This directly proves same-frame production and consumption for the covered
Lighting submissions. It does not infer readiness from a CPU readback: the
producer and consumers are ordered on the same D3D11 immediate-context command
stream.

## Eye attribution

All 1,691 matched calls were:

`DrawIndexedInstanced(..., InstanceCount = 2, ...)`

The bound VR Lighting vertex source accepts `SV_InstanceID` and calls
`Stereo::GetEyeIndexVS`. In VR, that helper returns
`StereoEnabled * (instanceID & 1)`, documented as eye index 0 for left and 1
for right. Runtime identity ties the loaded shader pack and DLL to the captured
build. The two instances therefore select the left and right view transforms;
these visibility-gated draws have derived attribution `eye: both`.

The same frame subsequently contains accepted OpenVR submissions for distinct
left and right resources in compositor cycle `26472`:

- left: `obs-resource-12201-g2`;
- right: `obs-resource-12214-g2`.

The capture does not yet contain a complete copy/resolve graph from the matched
draw targets to those final submitted resources. The submitted textures are
first observed around the runtime's final copy sequence. Thus draw-level
both-eye attribution is established from the proven stereo-instancing
mechanism, while the stricter draw-target-to-OpenVR-resource chain remains an
explicit render-map gap.

## Result and next targets

This run establishes that the covered path is active, same-frame, deterministic
at the observed resource version, stereo-instanced for both eyes, and measurably
reduces the covered exterior GPU span. It also narrows the remaining work:

1. observe the missing resource-flow edge between Skyrim's stereo draw targets
   and the textures handed to OpenVR;
2. complete the selected object's scene/material/`BSRenderPass` identity chain;
3. report fail-open reasons per draw family, determine why all 54 observed Grass
   setups failed visible, and then extend coverage one family at a time;
4. repeat controlled measurements in additional static exterior and foliage
   scenes before generalizing the performance result.

## Follow-up diagnostic prepared from this result

The first follow-up is implemented but was not deployed during the capture
described above. Depth-culling diagnostics contract `1.5`, schema revision `6`,
adds two family-partitioned outputs without changing visibility decisions:

- `drawSubmission.bindAttemptsByFamily` counts attempted Lighting,
  DistantTree, and Grass bindings;
- `failOpenReasonsByFamily` partitions every existing fail-open reason across
  those same families.

This preserves the original aggregate counters while making the next run able
to distinguish, for example, whether Grass is rejected because its object is
absent from the current native candidate set, belongs to a different depth
frame, or reaches the hook before the current result is ready. The instrumented
plugin built successfully as SHA-256
`50363FB668E2C5F0F60C8FDBEFCE3B9E791076C98D483517811E1F572D1B562B`,
and the focused counter test passed. A new runtime capture is still required to
populate these fields; none of the family-specific causes is inferred from the
older capture.
