# Output-merger main-menu capture, 2026-08-26

## Result

The output-merger target-observation slice passed its bounded live mechanism
gate at Skyrim VR's main menu. This run proves that target views and binding
sets are declared, reused, ordered, and joined to draws without inventing eye
or renderer-role semantics. It does not classify the broader set of targets
used by an in-game world scene.

The instrumented DLL was built from the dirty pre-commit state based on
`efdc2a88a006f3102a34f5ddfa19dbbceac224cb`; the identical source state was
then committed as `4530cc312`. The deployed DLL SHA-256 was
`00B616D29F8F0E1A31E1459D9334A544D893EE48929705E45A0B334EFE2D2137` and
the runtime build ID was
`d35b7423d7502c5eebcfe7b24ef41608d62d12a71ce378529362d2975017b759`.

## Capture bounds and completion

The controller requested 240 frames, 8,000 ms, 65,536 events, 32 MiB of
collector storage, and the maximum v1 target/shader catalogue bounds. Capture
`capture-live-00001444c50895a8-1` stopped on the 240-frame boundary with:

- 41,852 accepted events, sequences `0` through `41851`;
- no truncation and no dropped shader, stage-shader, target-view, or
  target-binding observations;
- one boundary rejection after the frame limit, as expected;
- a complete atomic manifest and `events.jsonl` artifact.

The serialized JSONL is 56,370,344 bytes. The configured byte bound applies to
the collector's bounded in-memory accounting rather than the expanded JSON
serialization size.

## Output-merger observations

| Observation | Count |
|---|---:|
| Immediate device contexts | 1 |
| Target-view declarations | 24 |
| Render-target declarations | 20 |
| Depth-target declarations | 4 |
| Unique binding observation IDs | 27 |
| Unique binding configurations | 26 |
| Output-merger bind events | 7,205 |
| Bind events reusing an existing identity | 7,178 |
| Explicit empty/unbound events | 721 |
| Draws joined to a target binding | 8,891 |
| Draws without a target binding | 0 |

Offline validation found zero sequence gaps or duplicates, zero immediate
command-stream regressions, zero unresolved target declarations, zero
unresolved binding declarations, and zero unresolved stage-shader declarations.
The capture therefore validates declaration-before-use, stable binding reuse,
explicit unbind representation, and draw joins. The runtime test separately
proves that `D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL` preserves the active
identity; a keep call intentionally has no distinct serialized target event.

All 41,852 events retain `eye: unknown`. This is the required honest result:
main-menu pointer alternation or target reuse is not evidence of a left/right
eye role.

## Shader-cache observation

The managed cache contained five files and 362,935,072 bytes before and after
the run. Both optimized pack files and both developer pack files were
byte-for-byte unchanged. Only `Info.ini` changed, so the long main-menu startup
was not a shader-cache rebuild.

## Retained evidence

The authoritative external evidence is retained at
`L:\Codex\evidence\render-map\20260826T120528Z-output-merger-main-menu`.
Large raw capture artifacts are not committed to the repository.

| Artifact | SHA-256 |
|---|---|
| `events.jsonl` | `D6E83EBEFE6740AD195203448405BC373EB235BB528310B0E8597ABE9C80C88B` |
| `capture-manifest.json` | `2B4D53E361C8007BD7E90BD4E317433AE2880F2E4A98C44E9DD39CAD419D6F47` |

The evidence directory also contains the API start/stop receipts, a successful
five-event paged retrieval, the exact MO2 winner receipt, the deployed build
manifest, before/after cache inventories, and the derived validation report.

## Remaining live boundary

The next capture must use a stable in-game world scene. It is required to add
world geometry, materials, shadow passes, water, weather, and feature-owned
targets to the observed set and to begin correlating those capture-local
identities with semantic engine roles. This main-menu capture should not be
used as a substitute for that classification run.
