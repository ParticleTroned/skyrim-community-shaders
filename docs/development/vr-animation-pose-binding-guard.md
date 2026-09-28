# VR animation pose binding guard

## Problem

Skyrim VR's animation pose copier trusts every entry in
`BShkbAnimationGraph::boneNodes`. The helper writes a 48-byte local transform
to each resolved destination without validating that the pointer is still a
live scene object.

Two full dumps contain that exact overwrite shape in unrelated victims. One
dump proves that the player graph's Shield entry pointed at a
`BSLightingShaderProperty`; the copied Shield translation is bit-identical to
the corrupt bytes. A later Papyrus victim contains another 48-byte transform.
The immediate engine writer is established, but the component that poisoned
the binding remains unknown.

An offline audit of all 126 entries in the captured player graph found 122
non-null bindings whose parent chains reached `graph->rootNode`, three null
entries, and exactly one binding outside the graph: entry 42, the shader
property victim. No legitimate captured binding violated the scene-membership
invariant used by this guard.

## Guard

On Skyrim VR 1.4.15, CSX verifies and hooks the call at RVA `0xAEC1AD` to the
native helper at RVA `0xB3C260`. For each invocation it derives the owning
animation graph, walks the graph's current scene tree and validates every
direct binding against that live object set. Entries with a non-negative
flattened-tree offset must also identify a `BSFlattenedBoneTree`.

If every entry is valid, the original helper receives the original arguments.
If an entry is invalid, the guard logs bounded graph, binding, pose and stack
evidence and calls the helper with a temporary copy in which only invalid
entries are null. The graph's original table is not changed. If the scene tree
cannot be validated or the temporary copy cannot be allocated, pose
application is skipped rather than permitting an unvalidated write.

The hook refuses installation if the runtime or original call instruction is
not an exact match. It also exposes the ownership name
`VRAnimationPoseBindingGuard`, allowing CSX to stand down after Engine Fixes VR
ships and reports the equivalent patch.

## Scope

This is containment and causal telemetry for `ISSUE-CSX-LIGHT-VALIDATION`.
It is not evidence that CSX caused the stale binding, and it is not a repair
for the unidentified producer. The final engine-level patch belongs in Engine
Fixes VR; any identified producer should also be corrected at source.
