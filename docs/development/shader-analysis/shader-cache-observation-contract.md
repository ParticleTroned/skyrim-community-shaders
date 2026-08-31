# Shader-cache observation contract

This is the hand-off contract for automation observing Stage 1 and collecting
evidence for Stage 2. Automation should observe and report; it must not infer a
successful selective invalidation merely from reaching the main menu.

## Required run identity

Preserve for every run:

- the CSX `producer` object, Build ID, DLL SHA-256, source commit, and dirty
  state;
- exact MO2 profile and enabled mod order;
- runtime (SE or VR), HMD mode, compiler identity, and CSX feature snapshot;
- the changed source path, feature toggle, or ABI identifier which defines the
  experiment; and
- start/end timestamps plus the bounded log interval used for analysis.

Do not compare captures from unmatched producer or profile identities.

## Cache evidence

Before launch and after compilation has completed, preserve an inventory of
`Data/ShaderCache` and, when present, `Data/ShaderCache.Previous` containing:

- normalized relative path;
- file size;
- SHA-256;
- last-write timestamp;
- `Info.ini` and `Manifest.json` as retained artifacts; and
- totals grouped by top-level shader family and extension.

Compute four sets between snapshots: unchanged, added, removed, and
content-changed. Report both file counts and bytes. Do not treat a moved
rollback generation as deleted evidence.

## Log contract

Capture CSX at debug level when per-entry evidence is required. Parse these
stable prefixes:

- `[ShaderCacheAction]` — startup validation, selective generation seeding,
  partial invalidation, conservative fallback, file-watcher decisions, and the
  one-time VR depth-culling in-memory refresh;
- `[ShaderCacheEntry]` — per-blob `reused` or `stale` result and its source/cache
  path;
- `[ShaderTiming]` — source compilation duration and queue behavior; and
- the compilation summary fields `disk cache` and `source compiles`.

Treat any `fallback=full-wipe`, `dependency-plan-failed`, manifest write error,
or cache generation race as a first-class result. Preserve surrounding log
context rather than silently retrying.

For VR depth-culling runs, `action=vr-depth-culling-refresh` may occur at most
once per process and must report `diskCacheAction=none` when applied. It must
not recur on interior/exterior transitions.

## Minimum experiment sequence

For each controlled change:

1. **Warm baseline:** unchanged inputs; expect zero source compiles and no blob
   mutations.
2. **Single change:** alter exactly one source, feature state, or explicit ABI;
   wait for compilation completion.
3. **Warm confirmation:** restart without another change; expect zero source
   compiles and identical active-cache hashes.
4. **Rollback when applicable:** restore the previous feature generation and
   confirm the original hashes return.

Compare the observed affected families and entries with
`shader-manifest.generated.json`:

- source changes use `invalidationIndex.bySource`;
- feature changes use the feature define's affected entry points; and
- no observed mutation may fall outside the expected impact set unless CSX
  emitted an explicit conservative-fallback event.

## Stage 2 evidence to accumulate

Retain enough information to refine family-level conservatism into exact
permutation-level decisions:

- complete macro set, entry point, stage, profile, descriptor, and compiler
  flags for every source compile;
- actual include-open sequence after preprocessing for that permutation;
- input contract digest and resulting DXBC SHA-256;
- bytecode equality across one-define A/B pairs;
- independently compiled utility/overlay program identity and lifetime; and
- whether each program was resident, active, recreated, or merely present.

The highest-value Stage 2 candidate is any family where the static graph says a
define is reachable but A/B preprocessing and bytecode prove that only a subset
of its permutations changes.
