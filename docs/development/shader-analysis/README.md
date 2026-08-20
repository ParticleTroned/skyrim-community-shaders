# Shader analysis

This directory is the durable CSX home for shader residency, recompilation,
pipeline ownership, and feature-state performance analysis.

- [`residency-recompile-optimisation-plan.md`](./residency-recompile-optimisation-plan.md)
  defines the architecture, milestones, guardrails, and measurement gates.
- [`shader-manifest.schema.json`](./shader-manifest.schema.json) defines the
  machine-readable feature, shader, pass, route, and resource vocabulary.
- [`shader-manifest.generated.json`](./shader-manifest.generated.json) is the
  deterministic static inventory generated from the tracked source tree.
- [`baselines/`](./baselines/) contains compact, sanitized, versioned summaries.
  Raw captures, compiled caches, DLLs, screenshots, and machine configuration
  stay in the external diagnostic archive.

Regenerate the static inventory from the repository root:

```powershell
pwsh -NoProfile -File tools/generate-shader-manifest.ps1
pwsh -NoProfile -File tools/generate-shader-manifest.ps1 -Check
pwsh -NoProfile -File tools/test-shader-manifest.ps1
```

The test verifies deterministic freshness, tracked-file hashes, inventory
counts, unique identifiers and paths, and the required manifest object shape.
It deliberately does not replace full JSON Schema validation; the schema is
the interchange contract, while the PowerShell test has no external package
dependency.

The generated inventory is deliberately labelled `inventory-only`. Static
paths and filename conventions cannot prove pipeline invocation, dynamic or
engine-owned resource bindings, include closure after preprocessing, or safe
independent scheduling. Those fields remain explicit unknowns until manually
annotated or exported from DevBench. A production optimization must not treat
an inventory-only record as a completed dependency classification.
