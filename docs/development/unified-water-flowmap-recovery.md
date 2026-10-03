# Unified Water flowmap loading and recovery

## Reported failure

Flowmap generation can complete successfully and still fail to reload the
new DDS when immediate directory discovery does not observe the producer's
path. Continuing initialization in that state publishes zero dimensions and
missing texture resources after native water has already been disabled.

## Origin

`a077f9e56bb54a6347b2d12bf49f34423bea3530`,
`feat: add unified water (#1634)`, dated 2026-01-25, introduced the
failure chain:

1. Regeneration clears the map and its dimensions before loading.
2. Startup continues after loading fails and disables vanilla water paths.
3. Geometry hooks test whether the `Flowmap` object exists, even when its
   texture is absent and its dimensions are zero.
4. Unified Water shaders divide UV coordinates by those dimensions.

The 2026-06-23 hardening commit `788e577fd` improved cached-file selection
and dimension validation. The 2026-07-03 rollback `b450bb2ac` removed
those improvements, but the original unchecked failure path predates
both commits. The wind-response commit `2d5876a43` from 2026-08-29 did
not introduce this flowmap failure.

## Recovery contract

-   Regeneration loads the exact DDS path returned by the producer, without
    relying on immediate directory enumeration. Each generation uses a new
    filename so it cannot overwrite the previous DDS or reuse its texture
    manager lookup key. Legacy filenames remain readable.
-   Cached DDS discovery accepts filename casing variations and ignores
    temporary files. Multiple matching caches trigger regeneration; file
    timestamps cannot establish which map matches the current source data.
-   Positive, bounded cell dimensions and a usable 2D texture/view must
    validate together. Reduced mip resolutions remain valid because their
    normalized UVs cover the same cells. Array and multisample textures
    cannot substitute for the shader's 2D texture.
-   Failed generation or loading preserves the active texture, dimensions,
    offsets, and previous DDS. Obsolete matching DDS files are removed only
    after the replacement loads. Enumeration and cleanup errors are logged.
-   Initial flowmap loading completes before executable patches disable
    native water. Hook readiness is published atomically only after the
    resources and flowmap binding are ready. Initialization failure leaves
    native geometry, flow updates, and LOD culling active.
-   All three replacement shader binding paths use the same readiness
    check. Compilation and cache warming continue normally. Unified Water
    being unloaded does not disable other custom water shaders.
-   Failed initialization can retry, and incomplete worldspace-cache builds
    retain the previous load-order hash so regeneration retries next launch.

These checks apply to SE and AE without runtime-specific branches.
No shader source, wind controls, or render-scale behavior changes.

## Adversarial review

The review corrected four weaknesses in the initial implementation:

1. A failure-only flag allowed custom shaders before initialization had
   completed. A shared atomic readiness state now covers both startup and
   failure and publishes resources after binding.
2. A shader-cache entry guard also prevented startup cache warming. The
   guard now applies only at the three actual shader binding paths.
3. Same-name generation could overwrite the retained disk cache and reuse
   an engine texture lookup key. New generation names and cleanup after
   successful validation preserve recovery. Ambiguous caches regenerate
   instead of selecting one by timestamp.
4. Exact full-resolution texture checks could reject valid reduced mips.
   Validation now accounts for them and rejects incompatible texture views.

Directory discovery and the shader binding predicate are each shared by
all their callers. Changes remain confined to flowmap loading, startup
publication, native fallback, and their regression coverage.

## Validation

`UnifiedWaterFlowmap` compiles production loading, startup, binding, and
readiness methods against simulated engine resources. Ten scenarios
passed: an unenumerated generated file, casing and temporary-file
filtering, failed regeneration preserving the active map, invalid metadata
and texture resources, startup failure/recovery, cached startup, reduced
mip resolution, replacement/ambiguous caches, readiness/late initialization
failures, and failed worldspace-cache builds retaining the previous hash.
The extractor checks that all three shader binding sites retain the gate;
this source check is not a runtime rendering test.

The host-side test is registered with the repository test suite. It
establishes failure handling but does not prove the original filesystem
cause or the in-game visual outcome. Live SE and AE validation should cover
fresh generation, cached startup, failed replacement, and retry behavior.
