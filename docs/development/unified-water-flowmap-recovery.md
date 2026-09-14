# Unified Water flowmap loading and recovery

## Reported failure

Two reports from the same CSX 3.19-VR producer show successful flowmap
generation followed immediately by failure to reload it:

| Supplied log               | Generation | Reload failure | Worldspace caches |
| -------------------------- | ---------- | -------------- | ----------------- |
| `CommunityShaders(98).log` | 2,421 ms   | 16:59:27.540   | 138/138 completed |
| `CommunityShaders(99).log` | 1,093 ms   | 14:18:02.867   | 43/43 completed   |

Both report Build ID
`fb7f4bf3080bd50fd7d5121a88b2a73413740e1022c2b913e73633228af24c55`.
Their original bytes were archived under `D:\Coding\GitHub\CS logs` and
verified against source length and SHA-256 before analysis. The logs do
not identify their exact compiled source commit.

The failing path rediscovered the generated DDS through directory
enumeration and a case-sensitive `Tamriel-Flowmap` prefix check. Every
later validation failure had an error-level message, whereas finding no
matching file logged only at debug level. Neither report contains a
later validation error. This points to failed file discovery; the logs
cannot distinguish filename casing from filesystem visibility or another
change to the directory between writing and scanning. Generation itself
reported successful texture capture and DDS publication.

## Origin

`a077f9e56bb54a6347b2d12bf49f34423bea3530`,
`feat: add unified water (#1634)`, dated 2026-01-25, introduced the
failure chain already present in `main-VR` history:

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

These checks apply to SE, AE, and VR without runtime-specific branches.
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

Commands used in the isolated worktree:

```powershell
pwsh -File ./tools/cmake.ps1 -D "PROJECT_ROOT=$PWD" -D "OUTPUT_DIRECTORY=$PWD/build/flowmap-validation" -P tests/extract_unified_water_flowmap.cmake
. ./tools/tool-environment.ps1
Initialize-CsxMsvcEnvironment -Required | Out-Null
cl.exe /nologo /std:c++latest /EHsc /W4 /WX /MD /I src /I build/flowmap-validation /Fobuild/flowmap-validation/flowmap-test.obj /Febuild/flowmap-validation/flowmap-test.exe tests/unified_water_flowmap_test.cpp
./build/flowmap-validation/flowmap-test.exe
cl.exe '@build/flowmap-validation/Flowmap.rsp'
cl.exe '@build/flowmap-validation/UnifiedWater.rsp'
cl.exe '@build/flowmap-validation/Hooks.rsp'
```

The three production translation units compiled successfully with MSVC,
`/W4 /WX`, and universal SE/AE/VR definitions. Response files use the
existing dependency include paths and isolated object outputs. They and
`*-review-compile.log` remain under the worktree's
`build/flowmap-validation` directory. The main checkout's binaries were
untouched.

Scoped whitespace, line-ending, clang-format, and Markdown checks passed.
The full-file CMake formatter would rewrite unrelated existing content;
only the new CMake block and extractor were formatted and checked.

Full DLL linking, deployment, and in-game validation have not run. The
tests establish failure handling; they do not prove the exact filesystem
cause or the visual outcome on either reporting user's machine.
