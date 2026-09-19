# Character NR edge and ROI stability corrections

The reported symptoms are a flickering band around selected face pixels and
occasional colour/brightness flashes within the neural region. Source review
found three concrete defects consistent with these symptoms. There is no
new headset capture tying a particular reported flash to one of them.

## Continuous optional feathering

The depth-aware feather kernel was centred on the nearest source texel.
Its integer-offset weights and finite stencil changed abruptly when source
jitter crossed a half-texel boundary. For one isolated face texel, flat
matching depths and radius 1, source positions 1.499 and 1.501 produced
feather weights 0.5 and 0. The continuous radial kernel produces 0.2505 and
0.2495 instead. These are analytic mask values, not measured game output.

`DLSS5CharacterMaskCS.hlsl` now measures distance from the fractional source
position and includes the complete radius-plus-one kernel. Samples enter
and leave with zero weight. A contribution ceiling skips saturated
interiors without the previous discontinuous coverage threshold. Existing
category exclusions, current-frame visibility, depth rejection, distance
fade and strength caps remain authoritative. No temporal mask is reused.
Depth-aware feathering remains optional and disabled by default; this
correction alone does not explain every outline artifact.

## Selection before and after DLSS

The reduced-resolution route composites character NR into the jittered
source image before DLSS. Its mask previously subtracted captured jitter,
misaligning selection and source colour before temporal reconstruction.
Mask preparation now explicitly identifies that jittered output grid and
uses zero sampling correction. Post-DLSS and final-scene routes retain
captured-jitter subtraction.

The same sampling correction drives the GPU shader and the CPU mapping of
current-source mask bounds. The output-grid flag participates in prepared
mask identity, finalization, diagnostic coverage policy and retained
capture evidence. Captured jitter remains unchanged in the provenance.
Crop, eye origin, authored depth and same-frame source checks are retained.

## One single-region envelope across readback availability

CPU geometry and GPU mask bounds previously updated independent stable
single-region envelopes. A ready/pending/ready nonblocking readback sequence
could therefore publish tight/broad/tight rectangles without scene motion.
Rectangle changes reset A/B provider history; C remains reset every call but
also receives a changing context. This is a plausible mechanism for
intermittent brightness changes, not a measured diagnosis of this session.

The planner now selects current required support before updating one shared
single-region envelope. Pending bounds still require conservative current
CPU coverage. Ready bounds cannot immediately undo that fallback growth.
The existing context headroom, 60-frame history, 30-frame contraction
cooldown and 25-percent saving threshold remain. Invalid or empty GPU
bounds do not mutate the shared fallback envelope. Crop, generation,
settings, ownership and explicit invalidation retain their reset behavior.

This may retain a larger inference region for the existing bounded history
window after a fallback. No speedup is claimed. Experimental two-region
ownership/planning remains separate; this change does not promise to remove
all split/merge transitions or change provider capacity policy.

## Preserved contracts and validation

No colour reconstruction, Lighting preservation, logical colour-filter or
edge-fade boundary, native automatic mask, neural strength/default, output
ownership or stereo publication policy changes. The character shader remains
VR-only at its existing caller boundary; SE/AE full-image NR is unchanged.
Preset schema revision and values are unchanged; only source compatibility
fingerprints are refreshed for the reviewed change.

Regression sources cover fractional feather phases and radius 0/1/4,
category/depth/distance exclusion, pre/post-DLSS selection alignment,
ready/pending alternation, safe growth, bounded shrink, invalid/empty
readbacks, frozen-source replay, owner changes, odd extents and screen edges.
The conservative bounds proof enumerates the new complete feather stencil.

No C++ tests, shaders, DLL or AIO were compiled, and no GPU or game test ran,
following the user's explicit no-build instruction. Source-only checks:

-   `pwsh ./tools/cmake.ps1 -P tests/neural_multi_roi_contract_test.cmake`: passed.
-   `pwsh ./tools/cmake.ps1 -P tests/neural_rendering_devbench_contract_test.cmake`: passed.
-   `python tests/neural_color/source_contract_test.py`: 8 passed.

The initial checks ran in an isolated review worktree based on `53190ba18`.
They were repeated successfully on `main-vr-nr` after the FOV alignment
commit `5327b66c6`. The changes retain that commit's shared mask geometry,
manual calibration and NR blend support. The route-grid decision still uses
the shared rendering-mode policy; no retired FOV controls are restored.

Earlier intermediate checks encountered an in-progress FOV schema mismatch
and a stale history reset-field ordering assertion; both source contracts
now pass. The FOV task separately reported that its controller compilation
found an unavailable helper in the new ROI regression source. The test now
constructs the full rectangle directly using the public descriptor type;
that correction has not been compiled in this task.

Next validation, after an explicitly requested build, is a short same-scene
HMD recording with the reported colour/rendering modes: compare stationary
and slowly moving face edges, then correlate any remaining flash with
current mask-bound availability, evaluated rectangles and reset reasons.
Also exercise reduced-resolution selection and a deliberately delayed-bounds
case. A live visual pass remains outstanding.
