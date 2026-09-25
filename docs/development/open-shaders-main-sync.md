# Open Shaders main selective review

## Scope and resume point

The 2026-09-25 review compares Open Shaders `main` with local `main-VR`,
oldest first, and requires an individual user decision before each port.
The pinned upstream endpoint is v2.15.0,
`0db03643c036de42077f8e9fa985e197c9604bb6`. The newer `dev` branch is outside
this review. Local review began at
`48b5a64f1824c7ccbba561b02686cb59dcc94b44` after the separate
[Community Shaders v1.9.1 review](upstream-main-sync.md).

The latest confirmed earlier Open Shaders port is #688, upstream
`5e3fe42ff2a48e6e59f7ac7bc9751496309ceefb`, carried locally by
`bab915a279b29586d1c1f9de19289ab614d619b0`. Its Tracy pin, hash and version
still match. Earlier ports #638, #644, #646, #656, #677, #679 and #685
place the previous work in the v2.13 release cycle. Complete v2.13
alignment is not established: #678 follows #688 and is absent locally.
Resume after #688, retaining earlier selections and exclusions. These are
selective adaptations; no upstream merge ancestry is claimed.

Exclude E11-only work, EHF, translations, Skyrim 1.7.99 support, upstream
repository housekeeping, SLF, Kevdev's new shared wind system and
Open Shaders-specific UI. Inspect mixed changes for independent useful
code. Retain Adaptive Balance instead of importing Scene Manager. Grass
optimizations remain deferred. Already covered changes are recorded
without another user decision. Compilation and runtime validation are
deferred until the selected-port exercise ends; no pushes are authorized.

## Decisions through #715

| PR                       | Decision                   | Code-level basis                                                                                                                                                                          |
| ------------------------ | -------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| #689                     | Excluded                   | Linux cache authentication workflow only.                                                                                                                                                 |
| #678 Procedural Sun      | User postponed             | Missing optional feature. Sky Sync and Adaptive Balance do not implement the procedural disc; a future port requires local sun identification, feature-buffer and stereo integration.     |
| #690                     | Excluded                   | Windows build cache workflows only.                                                                                                                                                       |
| #691                     | Excluded                   | SLF shadow atlas, renderer, scheduler and associated shader/version plumbing.                                                                                                             |
| v2.13.0                  | Excluded                   | Upstream release metadata.                                                                                                                                                                |
| #709                     | Excluded                   | HDR UI composition, preview, blur and associated resources. `HDRDisplay` is absent locally; its shared composition helper has no independent local caller.                                |
| #710                     | Already covered            | Native HMD dimensions are recorded before size overrides; `PerfModeState::RecordTrueHMDSize` and `VRPerfModeRestartState::Refresh` detect drift and recovery, with API restart reporting. |
| #711                     | Excluded                   | Nexus changelog publishing workflow only.                                                                                                                                                 |
| #713                     | Already covered            | `ShouldApplyDLSSSharpening` checks both strength and enabled mode across local foveated and standard routes; disabled paths retain copy/finalization behavior.                            |
| #712                     | Excluded                   | Scene Manager labels and translations.                                                                                                                                                    |
| #667                     | Inapplicable               | Guards an OS HDR screenshot capture path absent locally; local stereo capture has different ownership.                                                                                    |
| #715 Shared Win32 loader | User approved; implemented | Both local compiler include handlers used CRT-backed streams and needed the independent native reader.                                                                                    |

## #715: shared Win32 shader include loading

Port source: [Open Shaders #715](https://github.com/alandtse/open-shaders/pull/715),
commit `deab004df1527b3a74cd601e2b20c1a7dc8bf432`, authored by
Dlizzio `<77717521+Dlizzio@users.noreply.github.com>`.

`Util::ShaderInclude::Read` owns a Win32 file handle, validates the size
before allocation, and returns bytes only after a complete read. It does
not consume CRT stream/descriptor slots. Empty includes have a valid
zero-length buffer. Failures preserve the Win32 operation/error and byte
counts; diagnostics are deduplicated and capped at 128 distinct reports.

Both `CustomInclude` and `TrackingIncludeHandler` use this reader and
clear output pointers/counts before failure. `CustomInclude` retains its
root argument and default `Data/Shaders` root, adding optional source-path
diagnostics. Its `Close` deletes the correctly typed byte array. The
tracking handler retains normalized dependency identities, including
missing dependencies, and keeps include buffers until handler destruction.
Native reads use the filesystem path rather than the lowercased watcher
identity. Source/define hashes, cache provenance and watcher registration
remain intact. This path is shared by SE, AE and VR; no runtime relocation
or rendering permutation changes are needed.

The production source glob includes the new implementation. All four
existing tests that previously used the header-only loader are explicitly
linked to it and spdlog. The new `ShaderInclude` controller target also
extracts the actual tracking handler, covering both implementations under
CRT stream and descriptor exhaustion. It includes nested shader bytecode
comparisons, empty/missing includes, invalid arguments, bounded-error
diagnostics, file sharing, byte ownership, oversized sparse files, custom
roots and dependency/buffer lifetime checks. Compiled execution remains
pending; adding these tests is not evidence that they pass.

## Validation for #715

-   `pwsh ./tools/cmake.ps1 '-DPROJECT_ROOT=.' '-DOUTPUT_DIRECTORY=build/analysis/open-shaders-main-review-20260925/shader-include' -P tests/extract_shader_include.cmake`:
    passed; extracted the production handler without invoking a compiler.
    Initial absolute `-D` arguments were truncated at the Windows drive
    colon by argument forwarding; the relative form above resolved this.
-   Scoped pre-commit source/Markdown checks and `git diff --check`: passed
    after applying formatting to the changed source files.
-   Gersemi checks of the two new CMake fragments: passed with warnings for
    repository-defined `extract_between` and `add_controller_test` commands.
    The root CMake addition is checked by changed line only; the full-file
    hook is skipped to preserve pre-existing formatting outside this port.
-   Repository doctor: zero failures, one warning about the public HTTPS
    `open-shaders` remote. Authentication and origin access passed; no remote
    configuration was changed.
-   Not run, per user instruction: CMake configuration, C++ compilation,
    `ShaderInclude` and existing compiled tests, shader compilation, DLL
    packaging/deployment, and SE/AE/VR runtime validation.

## #716: non-PBR material access

The user rejected #716, commit
`ce9367f300edd1e95954a3b3bc4355dbd4a75699`. Its guards affect the OS
Advanced Skin feature, which is absent locally. Local Subsurface
Scattering is separate, and existing legacy material accesses already have
non-PBR guards. No code was changed for this item.

## Upstream sync merge 2e32921f0: rejected

Commit `2e32921f0a3e8ef24441505a71c7748eb213760b` merges upstream/dev as of 4163322011. Its first-parent diff contains seven files. Native-menu changes
are excluded, the SexLab version block retains the user's rejection of
upstream #2716, and the grass shadow-clamp removal is already covered in
current grass shaders (PBR grass remains postponed).

The Terrain Variation correction has a narrow transferable part. Loaded
landscape records already provide stricter eligibility locally, but
`TerrainVariationPolicy::IsLandscapeDiffusePath` falls back to every
non-tree subfolder under `landscape/` when records are unavailable. Current
tests explicitly admit `landscape/dirtcliffs/dirtcliffsroots01.dds` in this
fallback state. Upstream restricts directory matching to immediate files
under `landscape/`, avoiding this unverified eligibility.

The originating correction is upstream #2717,
`ec4f0eee4b78ff69aac5daabb48bd11025916e4e`, authored by Anthony
`<32780683+DwemerEngineer@users.noreply.github.com>`; Open Shaders retains
registered landscape paths alongside it.

After the user questioned the need for another path heuristic, the
recommendation changed from a partial port to **reject**. The earlier local
fix `5dcbf163ee08c0bbd652bd47eedd3dbbbec2d25e` already requires actual
record membership in normal operation. The startup `kDataLoaded` callback
collects those records, marks them available even when the list is empty,
and clears cached fallback decisions. The fallback applies before that
initialization or if the data handler is unavailable; no current gameplay
failure in that state has been established.

The hypothetical failure is stochastic UV sampling on an authored mesh
texture admitted only by its directory, disturbing its intended pattern.
The example filename appears in tests, not in a production NIF blacklist.
Restricting directory depth is still a heuristic and can also reject
legitimate nested texture paths during fallback. The normal record-based
solution is preferable; the fallback difference is not evidence that the
earlier fix needs this port. If future evidence warrants changing the
fallback, waiting for record availability would avoid another path rule.
The user rejected this item. No implementation, compilation or runtime
validation ran for it.

## #714: shadow pass-chain hangs

Excluded after inspecting commit
`b5a7cf6c094e9c6d3fbfb687dd8901fe608faa5f`. The chain guard, registration
tracing, stale accumulation cleanup and atlas invalidation all belong to
the excluded shadow caster manager. The generic skip-hook interface has
only that feature as its new consumer. Runtime protection is gated by the
manager's shadow-render window; extracting it alone would not protect
CSX's independent renderer.

The bundled CommonLib update was also inspected. The local dependency
`73b2384f7691dd04ab3d80d1fc9760bc8ae97811` already records a selective
review through v8.3.0 in its `docs/backports/6.7.1-vr-fixes.md`. Array-empty,
latent-return mapping and safe skin-bone access corrections are present;
local nullable VR menu accessors are more restrictive. Retain those earlier
selections rather than replacing the dependency. The additional v8.4.0
`ClearAllRenderPasses` binding is used by the excluded stale-pass cleanup.
No independently needed CSX change was identified.

## #719: engine overflow guards without SCM

Commit `1f8500016c84bfed179438e4106df777a251ee1e`, authored by Alan Tse
`<alandtse@users.noreply.github.com>`, moves alpha-group and culling-pool
guards out of the shadow caster manager and avoids duplicate installation
when Engine Fixes reports ownership. Despite the SLF title, the engine
checks do not require that system.

CSX already installs an alpha GeometryGroup ceiling through
`LightLimitFix::Hooks::InstallAlphaGeometryGroupGuard`, without an SCM
dependency. Preserve its runtime capacities and checked hook installation.
Before this port, CSX had no corresponding culling-pool exhaustion guard
or per-fix `EngineFixes_IsFixInstalled` query. Existing culling pointer and
lifetime guards address different failure modes.

The user approved the adapted partial port. It adds the missing pool
protection and shared ownership detection, keeping one alpha guard and
excluding shadow scheduling, caster filtering and SLF diagnostics.
Engine Fixes v7.9.0 source, preserved under
`build/engine-fixes-7.9-guide-20260924/source` at
`dc5a5c0adacbcace8cf8479c1eff9cb5714e1593`, confirms both fix names and the
export. Its pool guard also checks whether the append actually consumes a
pool entry; retain that distinction when adapting the more general OS
guard. Do not assume an installed version means a patch succeeded: query
the individual installed-fix result. Older or absent Engine Fixes requires
a locally validated fallback and hook-ownership checks.

`EngineFix::IsInstalledByEngineFixes` checks both loaded DLL names without
caching a negative result. The pool guard and existing alpha guard each
skip installation only when their own installed-fix name returns true.
The registry now accepts an explicit `TryInstall` result, so a declined
pool hook is not reported as installed; existing fixes keep their void
`Install` contract. The alpha guard retains its existing capacity,
counter decoding and checked detour as the fallback.

The pool guard checks both culling vtable slots for the same native target,
an executable-segment bound and the expected 25-byte prologue. Unreadable,
different, modified or unsupported targets are skipped with a diagnostic.
One checked detour wraps the shared entry, avoiding partial installation
across two vtables. Original-call storage is initialized before hook
publication, and installation failure is reported. The existing CommonLib
SE/AE/VR vtable addresses are used; no SDK or runtime-support update is
part of this port.

Only calls with the non-accumulation flag set or an alpha group index other
than -1 inspect the pool. Other appends retain their original behavior
even when the pool is empty. Pool-consuming calls are dropped below the
upstream 64-entry reserve, with a single warning. This is a conservative
precheck, not an atomic reservation; it retains upstream's concurrency
margin. Dropped work can omit geometry or shadow casters. No performance
improvement or reproduced CSX crash is claimed.

## Validation for #719

-   Saved VR 1.4.15 image inspection: both vtables at RVAs `0x1815B40` and
    `0x190BEB0`, slot `0x18`, point to `0xD99F80`. The expected prologue
    matches. The native function checks flag offset `0x301D5` and alpha
    index -1, uses pool offset `0x20150`, then writes through the returned
    pointer at `0xD99FD6` without a null check. The queue function at
    `0xD9B240` reads head/tail at `0x10000`/`0x10008`, masks 8192 entries
    and returns null when empty. This verifies the saved native ABI, not
    execution of the new hook. SE/AE rely on the matching upstream/Engine
    Fixes layout and the installation-time validation; no SE/AE image
    was inspected during this port.
-   Image provenance: snapshot `20260822T111246Z`, PID 39940, module base
    `0x7FF6F2B30000`, 60,133,376 bytes, SHA-256
    `1a6fbb7e726491929ea6eaa1dbfd866c48404edaefd63bd712f41fb13ffa2ce8`.
    Preserved image is under
    `build/validation/simple-coc-25x5s-20260925T091018479Z/live-snapshot/live-ghidra-snapshots/20260822T111246Z/SkyrimVR-live-pid-39940-base-00007FF6F2B30000.bin`.
    The bounded disassembler and output are
    `build/analysis/open-shaders-main-review-20260925/inspect-cull-snapshot.py`
    and `cull-snapshot-disassembly.txt` in that directory.
-   `pwsh ./tools/cmake.ps1 '-DPROJECT_ROOT=.' '-DOUTPUT_DIRECTORY=build/analysis/open-shaders-main-review-20260925/engine-overflow-guards' -P tests/extract_engine_overflow_guards.cmake`:
    passed without configuration or compilation. The new controller target
    uses the actual production guard and ownership query with native API
    and engine stand-ins. Cases cover missing/late/dual Engine Fixes
    modules, per-fix ownership, rejected targets, failed and repeated
    installation, argument forwarding, non-pool calls, the 63/64 boundary,
    unsigned counter wrap and one-time drop logging. These cases have not
    been compiled or executed yet.
-   Scoped pre-commit checks and `git diff --check`: passed. New CMake
    fragments and root line 1795 pass Gersemi checks, with warnings for
    repository-defined commands. As for #715, the full-root Gersemi hook
    is skipped after the changed-line check to preserve unrelated format.
-   Not run, per user instruction: CMake configuration, C++ or shader
    compilation, compiled controller tests, packaging/deployment, and
    SE/AE/VR runtime validation.

User-owned archives, logs, investigations, build outputs and shader caches
remain outside these changes.
