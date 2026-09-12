# Selective shader-cache invalidation: Stage 1

Stage 1 makes persistent engine-shader cache validity depend on actual shader
inputs instead of CSX release numbers. It deliberately does not claim that
independent utility and overlay programs are persistent-cache entries.

## Validity contract

An engine replacement blob remains reusable when all of these remain valid:

- its root HLSL and recursively resolved include-content digest;
- the global compilation mode and explicit custom shader defines;
- the managed compiler contract recorded for the cache;
- the explicit global shader-cache ABI; and
- any explicit feature-scoped shader ABI which reaches its shader family.

`PluginVersion` and ordinary feature `Version` values remain in `Info.ini` as
producer provenance. They do not themselves invalidate bytecode. A source
change is detected by the per-entry manifest. A C++ binding, resource layout,
or other non-HLSL compatibility change must instead bump one of:

- `config/shader-cache-abi.json` for a genuinely global incompatibility; or
- the owning feature's `GetShaderCacheAbiVersion()` for a change scoped by that
  feature's shader define.

Changing the explicit global contract causes one intentional full cache
transition when Stage 1 is first installed. Later unrelated CSX source edits do
not change that contract accidentally.

## Feature transitions

When a feature define is enabled or disabled, CSX now:

1. preserves the complete active generation as `ShaderCache.Previous`;
2. resolves which top-level engine shader families can reference the changed
   define;
3. copies only unaffected families into the new active generation;
4. carries forward only manifest entries whose blobs were copied; and
5. compiles the missing affected families on demand.

For example, `HORIZON_FIX` and `UNIFIED_WATER` reach `Water.hlsl`, so a change
retains unrelated Lighting, Grass, Effect, and other cache families. A runtime
family without a same-named HLSL root is treated as affected. If a recognized
source or dependency cannot be read safely, CSX keeps the complete rollback
generation and falls back conservatively to an empty active generation.

Feature-scoped ABI invalidation uses the same dependency plan without changing
the feature's enabled state. A global ABI or changed local runtime compiler
remains a full invalidation.

## Source changes and hot reload

Every cached engine entry already has a source/include content digest. A root
or include change therefore makes only entries using that closure stale.
Stage 1 also registers the static include closure when a blob is loaded from
disk; developer hot reload no longer requires that shader to have been source
compiled earlier in the same process.

An unclassified independent HLSL program no longer deletes the engine shader
cache. Independent-program persistence, conditional-permutation closure, and
safe live recreation are Stage 2 work.

## VR depth-buffer culling

Depth-buffer culling is runtime engine state, not a compiled-shader input. It
must not enter the persistent cache identity or create separate interior and
exterior cache generations.

Skyrim VR retains a 2024 compatibility requirement: ImageSpace shader objects
created before depth culling reaches its effective startup state must be
recreated. CSX now owns that operation in the VR feature, after applying the
current exterior/interior policy. The refresh:

- is requested only when effective depth culling is enabled;
- waits for active shader compilation to drain;
- clears only the in-memory ImageSpace class and preserves every disk blob;
- is cancelled if culling becomes ineffective while waiting; and
- runs at most once per process, so location transitions cannot cause repeated
  shader work.

This replaces the stale Volumetric Lighting hook which read Skyrim's raw INI
global before the VR feature applied the effective CSX setting.

## Compilation scheduling

The large pre-main-menu batch keeps cooperative background worker priority
until it has actually drained. `DataLoaded` and menu/UI initialization are not
used as proxies for completion: if the user releases the blocking wait into
background mode, the initial batch remains restrained until its last task.
Reused pool workers then restore normal relative thread priority for the bounded
in-game recompile pool, so later small runtime recompiles run at ordinary game
priority.

## Safety boundary

The family plan is conservative. It scans the complete textual include closure
and retains a family only when none of the changed tokens is reachable. Runtime
technique directories without a same-named HLSL root (notably remapped
ImageSpace families) are classified as affected. Unreadable recognized roots or
includes, empty feature defines, and filesystem errors refuse selective handling
and retain the existing broad fallback.
