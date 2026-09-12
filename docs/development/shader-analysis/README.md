# Shader cache analysis

This directory documents the selective shader-cache invalidation contract and
the evidence required to extend it safely.

- [`selective-cache-stage-1.md`](./selective-cache-stage-1.md) defines the
  implemented entry-validity, feature-transition, ABI, and fallback contract.
- [`shader-cache-observation-contract.md`](./shader-cache-observation-contract.md)
  defines the identities, cache inventories, logs, and controlled comparisons
  required when qualifying later refinements.
