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

The remaining review continues chronologically after #715. User-owned
archives, logs, investigations, build outputs and shader caches are outside
the port.
