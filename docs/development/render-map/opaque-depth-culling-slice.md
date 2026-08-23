# First vertical slice: opaque depth-culling identity and timing

## Objective

Prove an unambiguous, timing-complete path for one ordinary opaque object from
Skyrim scene identity to the final D3D11 draw in both VR eyes. Then determine
which culling decision points are actually reachable with current-frame or
previous-frame visibility.

This milestone proves the mapping and capture system. It does not require a new
culling implementation to be enabled.

## Candidate constraints

Start with one object that is:

- static and persistent for the capture window;
- ordinary `BSLightingShader` geometry;
- opaque, not alpha tested or blended;
- non-skinned;
- non-instanced;
- visible in both eyes;
- isolated enough to identify in a screenshot or RenderDoc capture;
- rendered in at least three consecutive frames.

The capture scenario must pin the save, cell, camera pose, weather/time, preset,
profile, executable, CSX build, and shader cache.

## Required identity chain

For each eye, the derived graph must connect:

```text
scene-object observation
  -> geometry observation
  -> shader-property/material observation
  -> BSRenderPass observation
  -> BSLightingShader technique/permutation
  -> shader-manifest pass/compile identity, or an explicit unmapped gap
  -> bound VS/PS and pipeline-state observations
  -> Draw* event
```

The chain must not depend on a process-global last-seen pass. Every edge must
cite event sequences and evidence. If more than one draw is plausible, the
report lists alternatives rather than selecting one by proximity alone.

## Required timing chain

The same capture must identify, where applicable:

1. scene-accumulation start;
2. visibility candidate generation;
3. depth source and view epoch used by the test;
4. visibility result ready;
5. result consumption;
6. pass construction/submission;
7. technique and geometry setup;
8. left-eye draw submission;
9. right-eye draw submission.

The report computes a decision window for these suppression stages:

| Stage | Potential saving | Decision deadline |
|---|---|---|
| Before scene/pass accumulation | CPU traversal, pass construction, and GPU work | Before the object is accumulated |
| Before batch submission | Sorting/submission and GPU work | Before the pass enters its batch |
| Immediately before `Draw*` | GPU draw work only | Before the D3D11 draw call |
| GPU predication/indirect arguments | GPU work after visibility is ready | Before predicate/arguments are consumed |
| Vertex-shader suppression | Primarily a correctness probe | Before vertex output; not expected to save meaningful submission work |

For each stage, the result is `viable`, `not-viable`, or `not-proven`, with the
event-order evidence that produced the answer.

## Instrumentation sequence

1. Add capture markers for frame, scene accumulation, and eye epochs.
2. Emit capture-local IDs for scene object, geometry, property/material, and
   `BSRenderPass` at the narrowest known engine boundary.
3. Add thread-local pass, technique, and geometry scopes.
4. Observe shader creation/binding and hash bytecode when available.
5. Track D3D11 device-context and pipeline-state identities.
6. Emit bounded draw events containing active scope tokens.
7. Emit depth-culling candidate, result-ready, view-validity, and consume events.
8. Join the capture without relying on pointer equality across frames unless
   object lifetime evidence supports it.
9. Review every ambiguous or missing edge before adding more object families.

## Acceptance gates

- The capture validates against the v1 schemas with no dangling manifest,
  engine, event, or observation references.
- Both eye draws are identified for the candidate in three consecutive frames.
- Pointer reuse cannot merge distinct observed objects.
- Nested or unrelated draws do not inherit the candidate's context.
- Dropped-event count is zero for the selected categories and frame range.
- Every required chain edge is directly observed or clearly marked correlated;
  no required edge is merely inferred.
- The visibility producer frame/view and consumer frame/view are explicit.
- At least one complete decision-window report is generated.
- Repeating the same bounded scenario produces the same semantic chain, while
  allowing capture-local IDs and raw addresses to differ.

## Expansion order

After this gate, add one exception class at a time:

1. opaque skinned geometry;
2. opaque instanced geometry;
3. alpha-tested foliage and trees;
4. grass;
5. distant/LOD geometry;
6. effect geometry;
7. particles and blended geometry;
8. water;
9. flat rendering and additional VR runtime routes.

Each class gets its own identity-chain and decision-window result. Passing the
opaque slice must not be generalized to a class with different scheduling,
depth, blending, or lifetime rules without evidence.

