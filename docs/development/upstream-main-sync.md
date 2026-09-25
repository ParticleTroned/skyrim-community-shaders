# Upstream main review after v1.8.4

Review upstream PRs chronologically against `main-VR`, implementing only
user-approved changes. Exclude E11-only changes, EHF, translations,
upstream-specific UI, and Skyrim 1.7.99 support. Inspect mixed PRs for
independent useful changes. Defer compilation and build-based tests until
the selected ports are complete, as requested by the user.

Skip presenting changes already implemented or better implemented in the
fork, and skip upstream repository housekeeping. Keep postponed work
separate from approved work to implement immediately.

Pinned upstream range: v1.8.4 (`02646c3008dd7cae91fc790c67c0f342a29aa938`)
through main (`5db085e77951b84bd6c191ff5b06d56893f08286`). The review starts
from `main-VR` commit `b6c7b431d79dc213a213d6632c589389eeba555d`.

## #2673: frame-generation allocator synchronization

Approved selective port of upstream commit
`c4b294f1a9cd201a8f4ebc1d3216d573f13b433f` by Shaun Ren.

`main-VR` already advances interop fence values before signaling and
handles counter exhaustion. The missing protection was a CPU completion
wait before reusing a D3D12 command allocator. Track each allocator's last
successful queue signal and wait before resetting that allocator. Retain
the tracking across buffer resizes and clear it on resource teardown.
Record submissions even when presentation returns a retryable result.
Wait failures use the existing proxy quarantine path. The existing VR
frame-generation exclusion is unchanged.

Validation:

-   `pwsh ./tools/git.ps1 diff --check`: passed.
-   `pwsh ./tools/pre-commit.ps1 run --files src/Features/Upscaling/DX12SwapChain.cpp src/Features/Upscaling/DX12SwapChain.h`: passed.
-   `pwsh ./tools/generate-unified-presets.ps1 -Check`: all three tiers verified.
-   Release DLL and focused interop-test build: cancelled on the user's
    instruction to defer compilation. No build or runtime pass is claimed.

## #2674: Skyrim 1.7.99 support

Excluded by explicit user instruction. No part of this PR was ported.

## Review decisions before #2719

| PR    | Decision                          | Code-level basis                                                                                 |
| ----- | --------------------------------- | ------------------------------------------------------------------------------------------------ |
| #2693 | Rejected                          | Map fog is covered; radial-blur replacements are disabled on all supported runtimes.             |
| #2698 | Excluded                          | EHF-only correction.                                                                             |
| #2688 | Postponed                         | Grass batching, culling and LOD require a dedicated VR port.                                     |
| #2648 | Rejected; covered                 | Existing virtual-function hooks provide stronger fallback and ownership handling.                |
| #2705 | Rejected; covered                 | Existing grass pixel paths already omit the shadow clamp.                                        |
| #2700 | Covered                           | Probe addressing uses signed modulo with runtime dimensions, including non-power-of-two tiers.   |
| #2707 | Rejected                          | Upstream repository housekeeping.                                                                |
| #2706 | Deferred with #2688               | UAV fixes belong to the postponed grass optimization implementation.                             |
| #2696 | Excluded                          | Translation fallback and translation CI.                                                         |
| #2711 | Excluded                          | Scissor adapter depends on the excluded Skyrim 1.7.99 compatibility contract.                    |
| #2709 | Approved, postponed               | PBR grass needs an independent stereo-aware port; retain unclamped shadows from #2716.           |
| #2712 | Covered                           | Capture precedes Present; staging has exception handling and encoder-slot cleanup.               |
| #2703 | Covered                           | Mesh terrain variation, parallax and material sampling already exist with stricter eligibility.  |
| #2701 | Excluded                          | Upstream HDR menu-blur path is absent from this fork.                                            |
| #2702 | Excluded; independent fix covered | Native menu injection is excluded; quality modes already use the fork's bounds.                  |
| #2714 | Covered                           | Blur bounds and both dispatch axes use the active area, with separate VR eye bounds.             |
| #2716 | Rejected                          | Blanket SexLab DLL version block lacks runtime-specific qualification.                           |
| #2717 | Covered or inapplicable           | Mesh eligibility uses landscape records and excludes trees; upstream HDR menu path is absent.    |
| #2721 | Excluded                          | Native menu lifecycle and audio fixes.                                                           |
| #2723 | Covered or inapplicable           | HDRDisplay is absent; DX12 presentation already retains the scene and clears only the UI buffer. |

## #2719: shader enablement indexing

Approved selective port of upstream commit
`fef3b94771d3dcb49298f36e21b7e5757c3e381a` by Kuzey Gök.

`State::ShaderEnabled` read `type + 1`, although configuration and UI
storage use `type - 1`. The native ImageSpace compute replacement also
read the Lighting toggle. Reject `None`, `Total` and out-of-range types,
then use the matching class index. Route vertex, pixel and native compute
replacement through that helper on SE, AE and VR.

Retain the native-water exceptions in both graphics hooks, shader-cache
fallback behavior, and VR volumetric dispatch and constant-buffer
restoration. Existing setting keys and array layout are unchanged.

Validation:

-   Source review checked the enum-to-setting mapping, invalid-type guard,
    both native-water exceptions and the unchanged compute dispatch body.
-   `pwsh ./tools/git.ps1 diff --check -- src/State.cpp src/Hooks.cpp docs/development/upstream-main-sync.md`: passed.
-   `pwsh ./tools/pre-commit.ps1 run --files src/State.cpp src/Hooks.cpp docs/development/upstream-main-sync.md`: passed after Markdown table formatting.
-   Compilation, compiled tests and runtime toggle checks remain deferred
    by user instruction. No compiled or runtime pass is claimed.

## #2722: background compilation wakeups and cache probes

Approved partial port of upstream commit
`c321154fa4d0b53e904dd756fd31c29cafc1679b` by 世界的山田.

Background-mode changes now exchange the atomic flag while holding the
dispatcher predicate mutex, then notify the dispatcher after releasing
the lock. Menu settings, the skip-compilation hotkey, the environment
override and the DevBench action use the same setter. Its previous-value
return preserves DevBench's atomic `changed` receipt. The registered tool
description and action schema describe the wakeup behavior.

A loose shader-cache existence probe uses the nonthrowing filesystem
overload. Probe errors are logged at debug level and treated as cache
misses, allowing the existing source-compilation path to run. Source-file
errors still use existing compilation-failure handling.

Retain the fork's content and include dependency validation, managed-pack
selection, in-flight dispatch counter and scope-based slot release. The
SSS keyword and geometry guards already cover the upstream corrections.
These shared changes apply to SE, AE and VR.

Validation:

-   Source review confirmed every background-mode writer uses the setter,
    which exchanges under the wait predicate's mutex and notifies after
    unlocking. DevBench still derives `changed` from the previous mode.
-   Parsed the registered tool descriptor with PowerShell `ConvertFrom-Json`;
    contract major 1 and the existing action list, including
    `backgroundCompile`, remain intact.
-   `pwsh ./tools/git.ps1 diff --check`: passed.
-   `pwsh ./tools/pre-commit.ps1 run --files src/ShaderCache.cpp src/ShaderCache.h src/Menu.cpp docs/development/upstream-main-sync.md`: passed.
-   `ShaderDevBenchBridge.cpp` has unrelated existing formatting drift.
    Restored the whole-file formatter changes and checked only the edited
    lines with cached clang-format 22.1.4:
    `--style=file --lines=237:237 --lines=431:431 --lines=440:440 --dry-run --Werror src/Api/ShaderDevBenchBridge.cpp`.
    This passed. Other applicable hooks passed for that file with only
    `clang-format` skipped. The commit hook uses the same skip after the
    scoped formatting checks above.
-   Compilation, compiled tests, runtime dispatcher wakeup checks and
    filesystem-failure injection remain deferred by user instruction.
    No compiled or runtime pass is claimed.
