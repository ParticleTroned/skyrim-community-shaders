# Material texture dependency slice

## Goal

This slice closes the first missing input edge beneath an observed Skyrim
material state:

`scene object -> geometry -> material revision -> runtime texture binding -> D3D11 resource`

It is a read-only observation layer. It does not alter material selection,
texture loading, shader binding, or cache policy.

## Runtime contract

At an existing geometry-setup boundary, CSX records the texture objects exposed
by the effective runtime material:

- Lighting materials use the material's ordered `GetTextures` list;
- Effect materials name source and greyscale roles explicitly; and
- Water materials name static reflection and the four normal roles explicitly.

The collector retains at most 32 bindings per material-state revision and at
most 383 UTF-8 bytes of each observed texture path. Both binding and path
truncation are explicit. The temporary Lighting enumeration buffer is 64
entries; the target Skyrim VR/CommonLib material implementations currently
return no more than 28 entries. A return beyond that supported boundary is
reported as truncation rather than being interpreted.

`bindingIndex` is the position in that runtime material list, or the documented
position assigned to an explicit Effect/Water role. It is **not** a claim about
an HLSL register, descriptor slot, texture-set form slot, or D3D11 SRV binding.

When a `NiSourceTexture` has a renderer resource, the runtime declares that
resource before the material event and stores its exact capture-local
`resourceObservationId` in the binding. The derived graph may therefore join
the binding to the D3D11 allocation without pointer equality. The
`NiSourceTexture` pointer remains supporting evidence only.

## Revision semantics

The material-state fingerprint includes each bounded binding's role, index,
path, `NiSourceTexture` evidence, D3D resource evidence, capture-local resource
observation ID, and path-truncation state. A texture swap or renderer-resource
replacement therefore produces a new state revision for the same
shader-property/material pair. Repeating the same effective bounded state
reuses the existing observation.

Failure remains fail-open for rendering: unavailable texture identity produces
no binding or a binding without a resource join. Catalogue overflow and
truncation make the evidence incomplete; they never block Skyrim's draw path.

## Derived graph

Each serialized binding becomes a `material-texture-binding` node. The graph
uses:

- `binds-texture` from the exact material revision to the binding; and
- `resolves-to-resource` from the binding to an earlier exact resource
  observation.

A binding that names an undeclared resource creates a blocking derivation gap.
This is an event-order or evidence-completeness defect, not permission to join
by pointer.

## Deliberate limits

This slice does not yet establish:

- the owning texture-set form or archive/loose-file provenance;
- source asset hashes or decoded texture contents;
- sampler state;
- the exact shader register or SRV slot consuming the material role;
- residency transitions or resource destruction; or
- semantic roles for every derived Lighting material feature.

Those are neighbouring slices. This one supplies the stable material-to-runtime
texture identity needed to investigate them without guessing.

## Live gate

After the source and contract tests pass, run short bounded Lighting and Effect
captures in a loaded scene. A passing gate requires:

- non-zero texture bindings for observed textured materials;
- every non-null binding resource ID declared earlier in the event stream;
- derived `binds-texture` and `resolves-to-resource` edges;
- no catalogue overflow, event loss, or binding truncation; and
- stable revisions for unchanged materials across the window.

Water is a separate targeted follow-up because a scene may not exercise an
observable water material in the same short window.
