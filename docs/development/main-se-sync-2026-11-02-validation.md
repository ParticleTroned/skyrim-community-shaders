# main-SE incremental synchronization: validation and review

Date: 2026-11-02. These results cover the final implementation and review
corrections for the 39-commit synchronization described in
[the functional report](main-se-sync-2026-11-02.md).

## Revision boundaries

| Role | Revision |
| --- | --- |
| Published SE baseline | `72424bfbee2dc03d416c9606269461f02c2aee0f` |
| Previous imported VR source | `f140c878138fe2783a6a4ad40420a3a2a996ccd0` |
| New VR source | `da73f53d8f6429d2d968cabde3c3eec9f3a04ab8` |

The ordered [commit inventory](main-se-sync-2026-11-02-commits.json) matches
`git rev-list --reverse` for the complete source range: 5 direct ports,
30 adaptations, 1 existing SE implementation preserved, and 3 VR-only
commits excluded. The SE baseline includes the published native build,
UI/HDR, presentation-hook and shader-cache follow-ups; those remain ancestors.

## Final results

| Check | Result | What it establishes |
| --- | --- | --- |
| Debug portable build | Passed | Strict host C++23 compilation of the registered portable targets |
| Debug CTest | **118/118 passed**, zero skipped | Final integrated host suite, including Python/CMake tests |
| Release portable build | Passed | Optimized host compilation with test assertions retained |
| Release CTest | **118/118 passed**, zero skipped | The same 118 tests under the Release build |
| Existing SE baseline suite | **42 cases, 25,132 assertions passed** in each configuration | Included as one of the 118 CTest entries |
| Real ImGui preset UI | **108 layout combinations passed**, plus interaction/path cases | Actual production UI with ImGui 1.92.6 docking and fmt 12.1.0 |
| Mono NR compression | **79,392 mapping/footprint/round-trip checks passed** | Odd, rectangular, reduced, supersampled and anisotropic geometry |
| Shader-cache Python suite | **14/14 passed** | Real SE inventory, feature defines, pack identity and package behavior |
| NR SDK cache Python suite | **5/5 passed** | Actual download/cache module against controlled local archives |
| Feature-version registry | Passed | Generated registry matches feature INIs and CORE markers |
| Localization | Passed | 4,595 English keys; JSON, ordering, stale-key and named-placeholder checks across 8 locales |
| Whitespace verification | Passed | `git diff --check` |

Debug and Release are two executions of the same suite, not 236 distinct
tests. Python test output was also inspected for nested skips. Both final
build logs contain no compiler warning/error diagnostics.

The isolated, unchanged SE baseline built successfully but passed only
98/99 CTest entries. Its existing upscaling UI fixture returned a `bool`
where production uses a scoped tooltip guard, triggering an unused-variable
error under `-Werror`. The follow-up correction models the real guard and
asserts balanced scope depth; it does not change production UI behavior.
The baseline run also lacked hlslkit for two nested Python checks. hlslkit
was installed for both final runs, and those checks executed successfully.

## Review findings and corrections

The review considered source-range coverage, mono adaptation, correctness,
resource/state ownership, failure recovery, hot-path work and duplication.
Findings were resolved in the integration or its explicitly attributed
follow-up commit.

| Finding | Correction and regression coverage |
| --- | --- |
| D32 target allocation can precede the first reversed world camera | Native consumers inspect the actual cached camera convention. Standard menu and orthographic cameras remain standard. Extracted production hook and SDK-boundary tests cover startup and same-frame transitions. |
| Current and previous presentation matrices can have different conventions | SE frame generation canonicalizes each matrix independently, including rotated/translated mixed history in both directions. |
| A failed DLSS evaluation can still allocate a context | Track context depth convention for every submission; recreate/reset when needed. Mark output history valid only after successful output, retaining the SDK's successful budget-warning case. |
| Standalone compute/pixel passes need the scene projection in b12 | Explicitly bind scene constants and restore the original pointer and suballocation window. Shared scoped helpers cover null/full/ranged bindings, early exits and D3D11 fallback; the optional compute-state capture does not add work to unrelated callers. |
| Cached texture descriptions can outlive the objects or transaction they describe | Use operation-local COM-owned snapshots, exact input identity and all-or-nothing pair capture; failed/null/replaced inputs invalidate the snapshots. |
| NR startup estimates and prewarm identity can diverge from actual inputs | Reuse finalized mono geometry/guide plans, exact projected prewarm keys, conservative reservations, post-sample timestamps and the correct submission fence. Production-method fixtures cover mismatches, first-evaluation ownership and bounded missing-telemetry retirement. |
| Captured NR diagnostics could describe later live state | Retain each execution's actual SDK inputs and face observation/admission. `Diagnostics(sourceFrame)` returns before live scene, SDK, memory or fence queries. The follow-up tests mutation, prewarm, missing/retired frames and owned JSON lifetime. |
| Preset export and UI regressions need real behavior coverage | Use the shared atomic writer and actual ImGui implementation. Verify failed replacement preserves the original, Unicode paths, cancellation, explicit-only transfers, bounded enumeration and balanced UI scopes. |
| The existing tooltip test did not model production RAII | Correct only the fixture and assert balanced guard lifetime; attribute the inherited fixture to the published SE baseline in the follow-up commit body. |

Performance changes are structural, not benchmark results: fixed shader
defines avoid temporary lists; source descriptions and finalized input plans
are reused within an operation; native NONE/TAA avoid unused vendor setup;
mono preparation dispatches color/guide rectangles once; the DLSS encoder
tiles depth reads; an unused luminance residual filter is skipped; and NR
success details are formatted on demand. No FPS, GPU-time or memory-saving
claim is established by these host checks.

## Supplemental shader compilation

Official Linux DXC 1.8.2505 was used for Shader Model 6 compilation. These
checks supplement the host tests; they do not replace native FXC compilation
or GPU execution.

| Variant group | Result | Source handling |
| --- | --- | --- |
| Depth/rendering, including culling, grass, terrain, GI, water, SSR, TAA, shadows and volumetrics | **94/94 passed** | Scratch-only cbuffer compatibility overlay |
| Mono upscaling, encoder, shared depth and sharpening | **20/20 passed**, warnings as errors | Unmodified production shaders/includes |
| Mono NR preparation, reconstruction, coverage and guides | **38/38 passed**, warnings as errors | Unmodified production shaders/includes, including developer experiment variants |
| Skin helper, production Burley path and aurora permutations | **12/12 passed** | Same scratch-only compatibility overlay |

The common depth matrix was also compared without any overlay. Both the
unchanged SE baseline and final implementation passed **62/88**, with exactly
the same 26 failure labels caused by DXC's handling of the existing namespaced
FXC cbuffers. There were no newly failing labels in that comparison.

The scratch adapter converts seven namespaced cbuffers to namespaced
struct/`ConstantBuffer` declarations with field aliases and removes
`packoffset` annotations in the temporary copy. It was never applied to
production files. This permits supplemental semantic compilation, but does
not validate the native buffer layout, SM5 bytecode or FXC behavior.

## Native tests added, not executed here

The maintained Windows shader suite now includes:

- `reverse_z_tests.cpp`: actual depth shaders, VS constant reflection,
  nearest-depth donor positions, far precision and standard startup cameras
  with the feature enabled.
- `grass_billboard_tests.cpp`: authored normal transport, both billboard
  faces, brightness and alpha across basic/enhanced/PBR/depth variants.
- `upscaling_depth_tests.cpp`: actual DLSS/FSR encoder and frame-generation
  copy shaders, partial tiles, donor ties, absent UAVs, output canaries and
  scene-constant restoration.
- `sss_skin_color_tests.cpp`: original SE scene color, male/female controls,
  alpha, linear energy, finite half-float gamut and depth-convention cases.
- Extended `device_context_state_test.cpp`: scene b12 pointer and range
  restoration through the optional state-capture mode.

These tests use the existing FXC/D3D11 WARP infrastructure and shared native
resource/reflection helpers. They were inspected but could not be compiled
or executed in this Linux workspace. PowerShell, the Windows SDK, MSVC,
native FXC and a game/GPU environment were unavailable.

In particular, a successful Windows plugin DLL build, the complete
game-dependent `MonoDevBenchBridge.cpp` translation unit, detour behavior,
native allocation rollback, live NGX evaluation, GPU performance and actual
SE/AE image quality remain unverified. Portable extraction tests compile
the underlying production methods/interfaces against narrow boundary fakes;
they do not establish that the entire native translation unit compiles.

## Dependencies and reproduction

The final host runs used GCC 13.3, CMake/CTest 4.4.4, nlohmann JSON 3.12,
Catch2 3.8.1, ImGui 1.92.6 docking, fmt 12.1.0, and hlslkit at
`682751d1d98ac13bab8e8a7e85b414b8b2a2f6a6`. Changed C++/HLSL ranges and new
files were formatted with clang-format 22.1.4.

The official NR SDK archive is pinned to SHA-256
`4bb8651b3ac2180f23df1f39422c489810a13d445a94dc836c876c18c93ecefe`.
The SDK parameter header and original license hashes were also verified
against `cmake/NeuralRenderingSDKPin.cmake`. Only headers and the license
were extracted. No NR runtime DLL was added to the repository or package;
the runtime remains user-supplied. The cache tests exercise the real CMake
module with small local archives, including offline warm cache, tampering,
header-only extraction, stale runtime rejection and preserved SE payloads.

From the repository root, with the verified SDK headers and JSON include
directory available:

```sh
cmake -S tests/portable -B build/portable-se-debug \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCSX_JSON_INCLUDE=/path/to/json/include \
  -DCSX_NR_SDK_INCLUDE=/path/to/verified/nr-sdk/include \
  -DCSX_PORTABLE_SE_BASELINE=ON \
  -DCSX_BASELINE_FETCH_CATCH2=ON \
  -DCSX_PORTABLE_FETCH_UI_DEPS=ON
cmake --build build/portable-se-debug --parallel 4
ctest --test-dir build/portable-se-debug --output-on-failure --parallel 4
```

Repeat in a separate build directory with `-DCMAKE_BUILD_TYPE=Release`.
Install hlslkit and its declared Python dependencies before running the
Python suite. Offline source trees can be supplied using
`FETCHCONTENT_SOURCE_DIR_CATCH2`, `FETCHCONTENT_SOURCE_DIR_CSX_UI_IMGUI`, and
`FETCHCONTENT_SOURCE_DIR_CSX_UI_FMT`; both final runs used these overrides.
The actual UI tests require the genuine libraries and do not substitute
layout mocks.

Native follow-up should use the repository's Windows build and shader-test
configuration, then verify SE/AE startup-to-world transitions, toggles,
render scales, NR fallback and frame-generation/HDR presentation in game.
