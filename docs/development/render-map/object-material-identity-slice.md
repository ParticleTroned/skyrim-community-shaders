# Object and material identity slice

## Goal

The current graph can identify a bounded draw's render pass, technique,
geometry scope, engine shader, selected D3D stage objects, targets, resource
versions and conservative dependencies. Its geometry identity is still only a
capture-local setup scope carrying pointer evidence. This slice turns the live
objects already present at `BSRenderPass` and `BSShader::SetupGeometry` into
bounded semantic catalogues:

```text
TESObjectREFR/base form
  -> BSGeometry
  -> BSShaderProperty + BSShaderMaterial state
  -> geometry setup binding
  -> draw
```

The objective is not to assign a friendly object name to every draw. It is to
state exactly which observed scene reference, geometry instance and material
state were connected at setup time, and to leave each unavailable link null.

## Why this is a separate identity layer

These values have different lifetimes:

- a `TESObjectREFR` form ID is stable only within the loaded data and save
  provenance that give the plugin index meaning;
- a `BSGeometry` pointer names one live scene-graph instance, but destruction
  and pointer reuse are not yet observed;
- a `BSShaderProperty` may be shared and its flags or material association can
  change;
- a `BSShaderMaterial` pointer may survive while its effective fields or hash
  are changed;
- a render pass binds one particular combination at one time.

Therefore the graph must not collapse them into one “object” node or treat a
pointer as a cross-capture identifier. The setup binding is temporal evidence;
the catalogues describe the values observed at that boundary.

## Capture-local catalogues

### Scene reference observation

`scene-object-observation-v1` is declared only when
`geometry->GetUserData()` returns a live `TESObjectREFR` at the existing setup
boundary. It contains:

- capture-local observation ID and pointer generation;
- reference pointer evidence;
- full reference form ID;
- base-object form ID when available;
- bounded reference and base-form names when available;
- whether either form ID is dynamic;
- explicit truncation flags.

Form IDs are evidence under the capture's plugin-load-order fingerprint. They
must never be interpreted against another load order. A null `userData` does
not produce a fabricated scene object.

### Geometry observation

`geometry-observation-v1` contains:

- capture-local observation ID and pointer generation;
- `BSGeometry` pointer evidence;
- bounded NiRTTI name and `BSGeometry::Type`;
- bounded `NiObjectNET::name`;
- vertex descriptor;
- world transform and world bound captured at observation time;
- linked scene-object observation ID, or null;
- explicit availability and truncation fields.

World transform and bound are observed state, not immutable geometry identity.
If the graph needs motion history, later observations create state revisions;
the first slice does not mutate the original declaration in place.

### Material-state observation

One `material-state-observation-v1` intentionally records the shader property
and base material together because the render pass presents them as one
effective setup input. It contains:

- capture-local observation ID and state revision;
- shader-property pointer evidence and bounded NiRTTI name;
- shader-property flags, alpha and engine material-type value;
- material pointer evidence, type, feature and `hashKey` when available;
- explicit null/availability flags for both property and material;
- a deterministic fingerprint over the retained values.

The material virtuals are read only at an existing setup boundary where the
engine is already using the object. Failure is fail-open: the setup proceeds,
the observation records unavailable fields, and no diagnostic exception may
escape into the renderer.

The same material/property pointers with a changed retained fingerprint produce
a new state revision. This prevents flag, material swap, feature or hash
changes from being projected backward onto earlier draws.

## Geometry setup binding

`geometry-setup-begin` advances to `geometry-boundary-v2`. In addition to its
existing 1.x fields, it carries:

- `geometryObservationId`;
- `materialStateObservationId`.

The geometry declaration links to the optional scene-object declaration, so
the fixed event payload needs only these two additional IDs. Existing pointer
fields remain for 1.x consumers but are supporting evidence, not the join key.
The draw already references its capture-local geometry scope; the graph follows
that scope to the v2 setup binding and projects only those exact observation
IDs.

## Bounds and hot-path rules

This slice adds independent start bounds and completion counters for:

- scene-object observations;
- geometry observations;
- material-state observations.

Each catalogue is fixed and preallocated before capture begins. A first-seen
declaration copies only bounded scalar/string data into owned storage. Repeated
lookups perform no allocation. Overflow makes the capture structurally
incomplete and never aliases a new object to an old observation.

Event selectors gain dependency expansion:

- `geometry-setup-begin` requires geometry, material-state and optional scene
  object declarations;
- requesting any semantic declaration without its setup consumer remains
  legal for inventory studies;
- requesting draws plus semantic setup data preserves the declarations needed
  to resolve every emitted observation ID.

A separate bounded selector is now implemented for high-density gameplay capture:

- optional numeric `geometryShaderTypes` (0 through 63) for geometry setup; and
- `executionWithinSelectedGeometry`, which records draw calls only while a
  selected geometry scope is active.

The resolved selector is part of the capture manifest. A draw excluded by this selector
is `filtered`, not `dropped`. The default remains the existing unfiltered
behaviour, preserving API compatibility.

## Graph projection

The graph adds:

- `scene-object`, `geometry`, and `material` nodes;
- `represented-by` from scene object to geometry only when the observed
  `userData` link exists;
- `uses-material-state` from geometry setup to the exact material revision;
- `same-observed-object` projections from execution to geometry, material and
  optional scene object through the setup scope.

Every edge cites the declaration and setup event sequences. Name equality,
pointer equality across captures, base-form equality, and matching material
hashes are not graph joins by themselves.

## Implementation order

1. Add catalogue records, bounds, overflow statistics and collector tests.
2. Add runtime declaration/binding methods using synthetic inputs; test
   revision and pointer-reuse behaviour without Skyrim types.
3. Extend v2 serialization and schemas; preserve all v1 fields.
4. Add graph nodes/edges and negative tests for missing declarations, changed
   material revisions and unresolved scene references.
5. Populate inputs in the existing Effect, Lighting and Grass setup hooks.
   Distant Tree remains explicit until its setup hook also owns a diagnostic
   scope.
6. Add the shader-type and scoped-execution selectors before a loaded-scene
   capture, rather than increasing the global event ceiling.
7. Run a short Lighting-only capture from a known Breezehome save and require:
   zero drops/truncation, every projected ID declared earlier, at least one
   named geometry, at least one form-backed scene reference, and at least two
   distinct material-state observations.

## Explicit non-claims

This slice does not prove:

- scene-graph construction or destruction;
- persistent identity across save/load, cells, captures or plugin-load-order
  changes;
- that every geometry has a `TESObjectREFR` owner;
- texture-slot contents or material-equivalence classes;
- skeletal partition, instance-group or per-triangle identity;
- material mutation causality outside an observed setup boundary.

Those remain later layers. The first useful result is a lossless bounded join
from live scene/reference and material state to the already-mapped draw and
resource graph.
