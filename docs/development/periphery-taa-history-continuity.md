# Periphery TAA history continuity

## Scope and behavior

The accepted Open Shaders
[#791](https://github.com/alandtse/open-shaders/pull/791) adaptation targets
`main-VR` after `9b7818e9737a4520c55113c67a470449b27ef3bd`.
It prevents foveated DLSS/FSR periphery TAA from reusing history that
predates skipped dispatches. It does not import upstream FoveatedRender
or change the previously rejected pause-menu routing policy.

The previous validity flag remained set when an eligible dispatch was
skipped without a resource or settings change. The new committed-history
record retains the last completed producer and its generation, method
and input/output dimensions. Reuse requires the existing temporal
snapshot policy to identify an immediately following matching producer.
Missing, repeated, skipped or incompatible producers reseed the history.

Main-pass rendering uses the engine frame. Submit rendering uses the
immutable captured producer, including compositor-cycle identity; desktop
Present between eyes cannot replace that identity with a newer frame.
Existing frame/cycle wrap behavior is retained. Resource recreation,
teardown and preserved-resource resets clear the record. Explicit history
reset requests continue to override otherwise consecutive history.

Both dispatch routes retain their existing stereo commit points. The
main pass commits after both eye composites succeed; submit commits when
both eyes are ready for the same producer. A skipped or incomplete pair
cannot advance the history record. No additional shader permutations,
GPU resources, passes, public settings or DevBench actions are introduced.
SE/AE dispatch paths are unchanged.

## Validation and pending qualification

The existing `VRSubmitTemporalSnapshot` controller target includes new
cases for first use, interrupted producers, frame/cycle gaps, repeated
producers, contract changes, peer-eye checks across Present, compositor
wrap, invalid records and explicit reset. These cases exercise the
production committed-history policy; their compiled run is deferred.

Source review checked both dispatch routes and all four reset sites.
The existing per-eye readiness guard and ping-pong commit order are
preserved. The resource-reuse fixture uses the production history record
and checks that reuse preserves its producer while recreation clears it.
Source-fixture extraction, scoped whitespace/Markdown hooks, clang-format
22.1.4 and `git diff --check` passed. The two large upscaling files were
formatted only on changed lines. The sync ledger records exact commands
and the source-extraction invocation correction.

The user deferred builds until the end of the sync. No DLL or controller
test build, shader compilation, deployment or runtime measurement has
been performed for this change. No runtime pass, performance improvement,
Build ID or render-scale qualification summary is claimed. Physical-HMD
qualification and matched performance/visual evidence remain pending.
Final validation should cover main and submit routes with DLSS and FSR,
failed or skipped dispatches, resumed consecutive frames, resource resets
and desktop Present between eyes.

There are no new measurements to publish in the
[numbered ledger](vr-render-scale-ledger.md). Historical measurements and
producer identities remain unchanged.

## Dispatch-result review correction

Adversarial review of `4a5d6f0cd9f8244c3a2f51e34c3f9ed7b420ad2a`
found that the low-level TAA dispatch could return without writing history
while its adapter still reported success. It now returns false on all
existing readiness/region guards and forwards that result through the
composite's failure path. Success follows dispatch and compute unbinding,
so the continuity record is not committed after an aborted history write.

The normal dispatch math, resources, frame/cycle ownership and reset
policy are unchanged. Source propagation checks and fixture extraction
passed. Compiled fault-injection tests, runtime transitions and performance
qualification remain pending under the user's no-build instruction.
No new measurement ledger or runtime pass is claimed; see the
[complete port review](open-shaders-217-adversarial-review.md).
