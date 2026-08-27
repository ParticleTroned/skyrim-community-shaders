# Shader analysis

This directory is the durable CSX home for shader residency, recompilation,
pipeline ownership, and feature-state performance analysis.

- [`residency-recompile-optimisation-plan.md`](./residency-recompile-optimisation-plan.md)
  defines the architecture, milestones, guardrails, and measurement gates.
- [`shader-manifest.schema.json`](./shader-manifest.schema.json) defines the
  machine-readable feature, shader, pass, route, and resource vocabulary.
- [`shader-manifest.generated.json`](./shader-manifest.generated.json) is the
  deterministic static dependency and invalidation graph generated from the
  tracked source tree.
- [`shader-dependency-report.generated.md`](./shader-dependency-report.generated.md)
  is the compact human review of coverage, feature-define reach, broad define
  declarations, compatibility variants, and remaining runtime evidence.
- [`shader-classification.annotations.json`](./shader-classification.annotations.json)
  records source-reviewed facts that cannot be safely inferred from naming
  alone. Generated files must never silently hide an unresolved edge.
- [`baselines/`](./baselines/) contains compact, sanitized, versioned summaries.
  Raw captures, compiled caches, DLLs, screenshots, and machine configuration
  stay in the external diagnostic archive.
- [`../render-map/`](../render-map/) defines the neighbouring version-specific
  Skyrim engine map, bounded runtime-evidence stream, and derived render graph.
  It joins to this manifest by stable compile/pass IDs without contaminating
  the deterministic static inventory with capture-specific state.
- [`selective-cache-stage-1.md`](./selective-cache-stage-1.md) defines the
  implemented entry-validity, feature-transition, ABI, and fallback contract.
- [`shader-cache-observation-contract.md`](./shader-cache-observation-contract.md)
  defines the identities, cache inventories, logs, and controlled comparisons
  required when qualifying later cache refinements.

Regenerate the static inventory from the repository root:

```powershell
pwsh -NoProfile -File tools/generate-shader-manifest.ps1
pwsh -NoProfile -File tools/generate-shader-manifest.ps1 -Check
pwsh -NoProfile -File tools/test-shader-manifest.ps1
```

The generator models the same merged virtual shader namespace used by
`tools/build-shader-cache.py`: `package/Shaders` first, then feature shader
packages in deterministic order. Its regression test stages that real
namespace and requires an exact path match. It also verifies deterministic
freshness, source hashes, unique identifiers, closed include/compile/pass
references, and a complete source-to-entry invalidation index.

The generated graph is labelled `static-classified`, not `runtime-verified`.
It closes all currently tracked production entry points against compile routes
and records direct/transitive includes, feature macro reach, resource-register
declarations, invocation evidence, overlay pipelines, and the Horizon Fix
structural cache variant. Static analysis still cannot prove dynamic or
engine-owned resource formats and lifetimes, fully assembled runtime define
sets, actual scheduling of every pass, bytecode identity, or safe unload
boundaries. Those gaps remain explicit under `unresolved.runtimeEvidenceStillRequired`.

One dormant stale include is accepted and remains visible: `RunGrass.hlsl`
references `WaterLighting/WaterCaustics.hlsli` under the unproduced
`WATER_LIGHTING` macro. The current feature uses `WATER_EFFECTS` and
`WaterEffects/WaterCaustics.hlsli`. Removing or migrating that dormant block
should remove the corresponding manual annotation.
