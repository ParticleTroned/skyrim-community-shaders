# Material input to execution correlation slice

## Question

For a draw with an exact prepared geometry/material observation, which bounded
runtime material texture identities are actually present in the effective
D3D11 shader-resource state, at which shader stage and register?

This is deliberately narrower than claiming that the HLSL samples a texture.
It establishes the state supplied to the draw. Shader reflection, bytecode
analysis, or a later dynamic observation is still required to prove that a
bound register is read by a particular instruction path.

## Existing evidence used

A completed capture must contain four independent facts:

1. `geometry-setup-begin` v2 names the exact material-state observation prepared
   by the selected `SetupGeometry` call;
2. each material texture binding names its exact capture-local D3D resource
   observation; and
3. ordered `resource-view-bind` events define the effective stage/slot SRV
   state inherited by the following draw; and
4. `draw-call-v3.preparedGeometrySetupObservationId` explicitly identifies the
   one-shot selected setup consumed by that draw.

The graph builder resolves each active SRV view to its resource allocation and
matches it against the active material's binding allocations. Matching is by
capture-local resource observation ID only. It never treats the material-list
position as a shader register and never matches paths or raw pointers.

## Graph contract

Derived render graph `1.8`, producer
`static-semantic-resource-graph-5`, enriches the existing resource-version
`reads` edge with `attributes.materialInputMatches`. Each exact match records:

- the material-texture-binding node ID;
- material-state observation ID and declaration sequence;
- material role, list index and bounded-list ordinal;
- resource and SRV-view observation IDs;
- SRV-view declaration sequence; and
- shader stage and slot effective at the draw.

`csx.materialInputExecutionCorrelation=true` advertises the join,
`csx.materialInputMatchCount` reports match occurrences, and
`csx.preparedGeometryDrawCount` reports draws using the explicit post-setup
handoff. A `prepares` edge records the association basis as
`same-thread-next-immediate-context-draw`. The semantic projection never claims
that the later D3D draw occurred inside the earlier C++ setup call.
`csx.preparedGeometryDrawCorrelation=true` advertises that distinction, and
`csx.samplerStateCoverage=false` prevents consumers from assuming sampler
state was captured.

The relation remains on the resource-version `reads` edge instead of adding a
shortcut edge between a material binding and a draw. That preserves the graph's
dataflow direction and acyclicity:

```text
material -> material binding -> resource allocation -> content version -> draw
```

Scene-object, geometry, and material identity projections now point from the
earlier observation to the later execution. This corrects the older reverse
projection, which became cyclic once material and SRV resource flows were
present in the same graph.

## What the correlation does not prove

- Sampler objects and sampler slots are not observed yet.
- A bound SRV may be unused by the selected shader control flow.
- One shared fallback allocation can legitimately match more than one material
  role or more than one register.
- A material binding absent from effective SRV state is not automatically an
  error; the engine may omit it for the selected technique.
- The current whole-resource version model remains conservative across
  subresources and D3D11 automatic hazard unbinding.

## Live gate

The first zero-loss loaded-scene Lighting capture proved why the older active
scope model could not satisfy this gate. Capture
`capture-live-0000fd1c2dfcdd10-6` retained 719 geometry and material
observations, 3,855 SRV binding events and no dropped catalogue entries, but it
contained zero draws: Skyrim returns from `BSLightingShader::SetupGeometry`
before issuing the D3D11 draw. The capture filter therefore rejected every
draw once the RAII geometry scope had ended. This is negative contract evidence,
not an empty scene or capture-budget failure.

Render-event 1.11 / `draw-call-v3` closes that boundary with a bounded runtime
handoff. Each selected setup replaces any older candidate. The next draw must
occur on the same thread, capture generation and observed immediate context;
it consumes the candidate exactly once. Every later geometry setup, including
an unselected shader type, invalidates the prior candidate. A context or
generation mismatch cannot consume it. The relation remains deliberately
narrow: an unrelated immediate-context draw inserted between setup and the
intended draw would consume the candidate, so the observed distribution must be
qualified against the Skyrim hook sequence rather than promoting this mechanism
into a general engine ownership rule.

Use one bounded loaded-scene Lighting frame and one Effect frame. Each capture
must include resource/view declarations, SRV binds, scene/geometry/material
observations, geometry setup, and draw execution. Qualify the slice only if:

1. the capture completes with no dropped catalogue entries or event loss;
2. every emitted `materialInputMatches` entry names declared material, binding,
   view, resource, and draw evidence;
3. the stage/slot relation is derived from ordered SRV state rather than the
   material binding index;
4. the graph remains acyclic and schema-valid; and
5. unmatched roles and shared-fallback multi-matches are reported as observed
   distributions, not silently classified as defects.

The loaded-scene Lighting qualification met these gates. It captured 321 setup
pairs and 321 later draws, with 321 unique declared prepared-setup references,
zero missing references, zero dropped events/catalogue entries, and 1,305
derived material-input matches in an acyclic graph. The retained artifact and
hashes are recorded in
[`material-input-execution-live-capture-2026-08-29.md`](./material-input-execution-live-capture-2026-08-29.md).

The following sampler slice should instrument all immediate-context
`*SetSamplers` calls, catalogue immutable sampler descriptors, carry effective
sampler state into draw/dispatch execution, and only then relate a texture SRV
slot to the sampler slot selected by shader reflection or bytecode evidence.
