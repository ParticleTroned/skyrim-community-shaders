# Object/material identity — Breezehome Lighting capture — 2026-08-29

## Result

The object/material identity slice passed its first bounded live world-scene
gate. A two-frame, Lighting-only capture retained typed scene-object,
geometry, material-state, and geometry-setup identities without event loss or
catalogue overflow. The derived graph validates, is acyclic, and uses only
capture-local observation IDs for its semantic joins.

This closes the earlier generic-graph gap in which a setup scope could be tied
to an engine shader and render pass but not to the Skyrim object, geometry, or
material state being submitted.

## Runtime provenance

| Item | Exact value |
|---|---|
| capture | `capture-live-0000f5b621d8089c-4` |
| Skyrim runtime | `SkyrimVR.exe` 1.4.15 |
| CSX source commit | `b223489b6b2afb5441409b3e17e826ff964efb50` (dirty working tree) |
| deployed DLL SHA-256 | `F0E728AED61733CD224F4FAEEE45D3A8329855B1BD5EC30AF4D1A27EDF2795DE` |
| DevBench build ID | `95ee84a89fbf2634a2d6a7f7bf4f19b48adb45379d92542d62c8799a509712ee` |
| fixture | verified Breezehome interior save, `Save2_2FAD4605_0_507269736F6E6572_WhiterunBreezehome_000005_20260822173026_1_1` |
| VR route | Valve null HMD plus qualified Codex head-pose driver |
| shader family selector | engine shader type `6` (`BSShader::Type::Lighting`) |

The in-process manifest still reports the generic scenario and MO2 route as
unavailable. Exact profile, save, deployed-artifact, process, and null-HMD
identity are therefore supplied by the retained harness receipts rather than
invented in the capture manifest.

The capture-start shader-compilation context was observed: optimized VR mode,
shader-cache ABI
`85f71f8f713542badba1027e6216e07f42a762782165d185f004e4c14af1d5c6`,
compiler `d3dcompiler_47.dll:10.0.26100.9168`, and global compile-state digest
`d6eddee746b2c504a4388fdfb5cdd1bd`. No external compatibility registrations
were present in this run.

## Bounds and completion

The request selected object, geometry, material, and geometry-setup events,
plus frame markers, with:

- `geometryShaderTypes: [6]`;
- `executionWithinSelectedGeometry: true`;
- two frames, two seconds, 32,768 events, and 32 MiB;
- independent catalogue limits of 1,024 scene objects, 2,048 geometries, and
  2,048 material states.

Completion was `frame-limit`, not event or byte exhaustion:

| Measure | Result |
|---|---:|
| accepted events | 4,601 |
| dropped events | 0 |
| truncated | false |
| scope overflow / mismatch | 0 / 0 |
| scene objects | 254 |
| geometry observations | 727 |
| material-state observations | 724 |
| filtered events | 82,676 |

Filtering is intentional selector work, not evidence loss. One event arriving
after the two-frame boundary was rejected and reported separately; both CPU
frame IDs (`30478`, `30479`) are retained on the accepted events.

## Semantic evidence

All 4,601 event envelopes validate against `render-event` 1.x. Every
non-declaration observation reference resolves to a declaration at a lower
sequence number. The stream contains:

- 254 form-backed scene-object identities, 98 with resolved reference/base
  names and 141 distinct base forms;
- 727 named geometries: 726 `BSTriShape` and one `BSDynamicTriShape`;
- 724 distinct material-state fingerprints across 724 shader properties;
- material features `0` (700), `1` (17), `11` (6), and `5` (1);
- 1,448 balanced Lighting setup scopes, spanning 32 numeric pass enums, two
  render-flag values, one Lighting shader object, and 724 render-pass objects.

The two-frame window did not observe a material-property revision. That is an
honest negative result, not evidence that material state is immutable. A later
controlled mutation capture is required to validate revision increments and
the graph's exact-revision join across a change.

The derived graph contains 3,154 nodes and 3,623 evidence-bearing edges:

| Graph item | Count |
|---|---:|
| scene-object nodes | 254 |
| observed geometry nodes | 727 |
| temporal geometry-setup nodes | 1,448 |
| material nodes | 724 |
| shader-compilation-context nodes | 1 |
| `represented-by` edges | 727 |
| `same-observed-object` edges | 1,448 |
| `uses-material-state` edges | 1,448 |

There are no ambiguities, and `csx.graphAcyclic` is true. The two remaining
graph gaps are expected for this deliberately semantic-only capture: no typed
resource declarations were requested, and resource hazard reconstruction is
still allocation-wide rather than exact subresource overlap.

## Contract findings

The live run exposed two service-description issues:

1. The parser and registry response supported `geometryShaderTypes` and
   `executionWithinSelectedGeometry`, but the authoritative MCP input schema
   omitted both properties. The descriptor and a source-contract regression
   check were corrected after the run.
2. `frame-begin` and `frame-end` were requested and reported as resolved, but
   no explicit frame-marker events were emitted. Per-event `cpuFrame` still
   proves the two-frame partition and drives the frame limit. After the run,
   the advertised selectable inventory was narrowed to implemented emitters;
   boundary and command-list scaffolding without a live emitter is now exposed
   separately as non-selectable `plannedEventKinds`.

The first attempted broad draw capture also demonstrated why bounded capture
shape matters: dependency-expanded `resource-view-bind` traffic exhausted the
byte budget before semantic catalogues became useful. A semantic-only selector
with small unrelated catalogues is the correct collection shape for this
question.

## Retained evidence

Authoritative directory:
`L:/Codex/evidence/render-map/20260829-object-material-lighting`

| Artifact | SHA-256 |
|---|---|
| capture manifest | `A9F74B54B6F2CB3E613AE0049709307B7D9106C8B8E7CB373703A29626EE6744` |
| events | `97420B5AEB4E3D90852302F7CE25C17EF581919D93E5ECB13D84521B136EB699` |
| derived graph | `A0699AD61351E335615BF26916B5A2C18310D292BEEB1E937A5A37E222316695` |

The `receipts/` subdirectory retains the exact MO2 workspace/session,
verified fixture load, deployed runtime binding, capture-stop, and null-HMD
receipts. Large capture and graph artifacts are retained as evidence rather
than committed to Git.

## Next gate

The next identity gate should mutate one bounded material property between two
captures and prove that the property retains its semantic identity while the
material-state revision and fingerprint change. In parallel, the event-kind
registry needs an implementation-backed capability inventory so an advertised
kind cannot silently produce no events.
