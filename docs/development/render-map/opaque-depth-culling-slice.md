# First vertical slice: opaque depth-culling identity and timing

## Objective

Prove an unambiguous, timing-complete path for one ordinary opaque object from
Skyrim scene identity to final D3D11 work covering both VR eyes. Then determine
which culling decision points are actually reachable with current-frame or
previous-frame visibility.

This milestone proves the mapping and capture system. It does not require a new
culling implementation to be enabled.

## Current gate

Draw, dispatch, immediate-context command order, output-merger targets, typed
resources and views, copy/resolve flow, and shader selection are implemented and
live-validated. The depth-culling producer and consumer now emit a targeted
resource version, verify the effective VS SRV binding, and carry an explicit
submission observation into the eventual `Draw*`; closed setup scopes are not
used as a proximity heuristic. Accepted OpenVR submissions identify the final
eye texture and bounds without assuming that earlier work used two physical
draws.

The present gate is a live decision-window capture that proves the native OBB
result buffer, the candidate-to-draw association, and the resource path from
the selected draw to an eye-submitted texture. See
[`depth-culling-observation-sequence.md`](./depth-culling-observation-sequence.md).

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

For the work covering both eyes, the derived graph must connect:

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
8. draw or dispatch submission covering the first eye;
9. draw or dispatch submission covering the other eye, unless one stereo route
   is proven to cover both.

Coverage may be two per-eye draws, a single stereo draw, instancing,
array-target rendering, or another evidenced VR route. The acceptance gate does
not assume two physical draw calls.

Every visibility-ready and decision event identifies its execution/readiness
domain. CPU QPC order proves CPU observation and submission order only. A
decision window may claim GPU viability only from GPU command-stream ordering,
GPU completion evidence, resource-consumability evidence, or completed CPU
readback appropriate to the proposed suppression mechanism.

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

Completed foundations:

- typed, generation-safe pass, technique, geometry, shader, context, target,
  resource, view, and resource-version observations;
- bounded `Draw*`/dispatch events and monotonic immediate-context order;
- explicit visibility submission identity consumed by exactly one `Draw*`;
- effective VS SRV slot verification after D3D11 hazard handling;
- completed diagnostic readback decisions joined to resource version and object
  index;
- accepted OpenVR eye/resource/bounds observations.

Remaining sequence:

1. Seed the first non-example engine map from the existing capture's shader
   types, caller RVAs, pass values, and descriptor combinations.
2. Accept exact externally verified build, manifest, runtime, profile, cache,
   and scenario provenance in the capture-start contract.
3. Add capture markers for frame and scene accumulation epochs.
4. Emit capture-local IDs for scene object, property/material, and
   `BSRenderPass` at the narrowest known engine boundary.
5. Join the selected draw's output resource path to the accepted OpenVR eye
   submission; preserve `eye: both` where one stereo submission is proved.
6. Join the capture without relying on pointer equality across frames unless
   object lifetime evidence supports it.
7. Generate and review the decision-window report before adding more object
   families.

## Acceptance gates

- The capture validates against the v1 schemas with no dangling manifest,
  engine, event, or observation references.
- Both-eye coverage is identified for the candidate in three consecutive
  frames, including the stereo mechanism and number of physical submissions.
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
