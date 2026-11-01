# main-SE adversarial implementation review

Date: 2026-11-01. Branch: `main-SE`.

## Reviewed revision and scope

The complete implementation was uncommitted when this review began. It was
recorded first as **`64229b11ea47436a98ba87ea55c5b4fefc43d686`**
(`feat(se): sync shared rendering and UI`), preserving an immutable target for
the requested follow-up fix. That snapshot contains 866 changed files relative
to SE parent **`e1989826771a1faf9fbb9e7e77a3c38e2256051d`**. Its imported
source is main-VR **`f140c878138fe2783a6a4ad40420a3a2a996ccd0`**.

The review covered the complete functional integration across rendering,
upscaling/NR, presentation and frame generation, culling/streaming, materials,
water, shader caching, services, settings, editor/UI, diagnostics, and build/test
delivery. It assessed scope, correctness, failure/lifetime behavior, avoidable
work and duplication. Parallel reviews used the SE parent and pinned VR source
to distinguish retained SE behavior, imported behavior and port mistakes.

The follow-up commit containing this report references the implementation SHA
in its **body**, not its title. Changes remain local; no push, game deployment or
release version change is part of this review.

## Findings and corrections

### Build blockers

| Finding                                               | Why it failed                                                                                                                                                                                                         | Correction and regression coverage                                                                                                                                                                                                                                                                            |
| ----------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Invalid Wetterness lighting parameters                | Three production helper signatures contained literal `float 1.0, bool true` declarations after removal of VR foveation arguments. The corresponding lighting permutations cannot compile.                             | Remove the obsolete parameter plumbing from declarations and calls. Preserve the remaining wetness calculations. Add combinations of Wetterness/legacy Wetness Effects, PBR, skylighting and deferred lighting to the native shader test inventory.                                                           |
| Duplicate material helpers                            | The port introduced a second global `ReconstructTBN`; wetness includes also carried duplicate normal reorientation helpers.                                                                                           | Reuse the existing shared implementations in `Common/Shading.hlsli`, including the water callers. This removes duplicate definitions and duplicated math.                                                                                                                                                     |
| Missing sky declaration include                       | `Sky.hlsl` still referenced `Permutation` after its declaration include was removed.                                                                                                                                  | Restore the dependency and add a focused HDR-sun sky permutation to the native inventory.                                                                                                                                                                                                                     |
| Duplicate SSS C++ helper                              | `ClampHumanSkinControl` appeared twice; the first copy additionally referenced constants before declaration. Earlier UI extraction compiled only selected functions.                                                  | Remove the stray copy, preserving the existing SE implementation. A new fixture compiles the complete anonymous helper namespace with the real settings and JSON types. The baseline fails compilation; the corrected region compiles and passes behavior checks.                                             |
| Missing SE water diagnostic callback                  | Light Limit Fix retained its call to `LightEditor::ObserveWaterPass`, while the port removed the declaration, implementation and diagnostic display.                                                                  | Restore the coherent SE observer, lifecycle and diagnostic UI using existing native owners. This preserves the SE facility and repairs the missing member call without adding an engine hook or address.                                                                                                      |
| Removed SE integration methods with surviving callers | DataLoaded still called `WriteDiskCacheInfoWhenReady`, the editor called `HasWeatherSettings`, legacy CSUtility called underwater DOF `ClearShaderCache`, and weather controls called a removed transition-query API. | Reconnect deferred cache metadata publication at the render-thread maintenance point, restore the weather-presence query and DOF reset contract, and use the current WeatherManager ownership query for UI controls. Cache readiness, disabled/revert/save-load gates and the active DOF owner are preserved. |

These are source defects, not inferred GPU performance problems. The earlier
84 portable tests genuinely passed, but did not compile the complete Windows
plugin or semantically compile every production shader. Preprocessing alone
did not detect the HLSL errors. This review expands coverage rather than
reclassifying those earlier checks as a native build.

### NR, render dimensions and presentation

1. **Final NR selected an unused HDR texture during SDR output.**
   `Main_PostProcessing` treated a loaded HDR Display feature as proof that it
   had redirected the framebuffer. Redirection can instead decline because
   output is SDR or a resource/shader is unavailable. NR then performed work on
   stale HDR content without changing the displayed scene. `RedirectFramebuffer`
   now returns its actual result, which determines the final NR source.

2. **View-backed SE framebuffers silently skipped NR.**
   Native framebuffer slots can expose their texture through SRV/RTV views
   while their direct texture field is null. The new final NR and prewarm paths
   used only the direct field. The existing screenshot resolver is now shared
   as `Util::ResolveRenderTargetTexture` and reused by all three consumers.
   Queried COM references remain scoped to the call; no swap-chain resource is
   cached across frames.

3. **Resolution planning could lose one pixel.**
   The new plan promoted a stored float scale to double before multiplication,
   whereas SE's `ConvertToDynamic` uses float multiplication before truncation.
   With the production Ultra Quality preset, 2560-wide output produces 1969
   native pixels but the old plan returned 1968; 1080-high output produces 830
   instead of 829. The plan now matches the native arithmetic while retaining
   finite/range checks. Tests compare all seven quality presets over six
   resolutions and cover invalid and maximum-integer bounds.

4. **Screenshot evidence used the next frame.**
   `State::Reset` advances `frameCount` before the presentation chain; the
   rendered scene, NR acknowledgment and captured exposure belong to the frame
   before that increment. The Present wrapper now carries that source identity
   explicitly through screenshot processing, acquisition and evidence lookup.
   The SE Present/FG ordering and TEST-call bypass remain intact.

5. **Manual/identity NR frames inherited stale exposure provenance.**
   A retained captured-exposure binding was copied into later snapshots even
   when the current color pass did not sample it. Each evaluation now records
   only the exposure stamp actually used by that pass. Retained GPU resources
   are preserved safely; unused evidence is cleared.

The two production-extracted NR fixtures independently fail against the
baseline for `scene`, `alias`, `dimensions`, `frame` and `exposure`, and pass
with these changes. They execute the actual hook, HDR redirect/restore, final
application, dimension, screenshot and evidence code with observable engine,
provider and COM boundaries.

### Resource failures and object lifetimes

**Volumetric shadow setup could throw into rendering or publish an incomplete
fallback.** Sampler/texture/SRV creation now rolls back the complete fixed
resource set on failure. The texture/SRV pair publishes transactionally. A
failure latch prevents repeated allocation attempts every draw until a shader
reset or device change. Shared shadow consumers remain disabled until their
sampler and fallback moment map are ready; a null moment map is not treated as
a lit fallback. Fault injection covers each allocation stage, repeated draw
attempts, explicit retry, device replacement, orphaned pairs and reference
release. The State gate was checked against resource setup and draw ordering.

**Water deduplication confused a position bucket with surface identity.**
Quantized position/scale collisions could remove distinct geometry, rotation,
water types or materials. Repeated registrations of one shape could even cull
the shape that was supposed to survive. The corrected comparison requires
distinct shapes with matching native transform, renderer data, vertex layout,
material properties, water type/flags/plane and mesh counts, plus a verified
nonnull LOD-root relationship. Skin/dynamic ambiguity is preserved. Intrusive
owners keep removal candidates alive through native removal, and each shape
is removed at most once. Scratch ownership is released after the operation.
The regression compiles the production loop and real CommonLib transform,
vertex descriptor and enum contracts; it preserves distinct/aliased/no-root
cases and removes only proven equivalent LOD copies.

**Shader cache storage could die before its users.** The port's managed-pack
function-local static was first constructed after the cache singleton. Reverse
static destruction therefore released stores before worker joins and the final
manifest flush. The loose-manifest owner had the same adjacent issue inherited
from the SE parent. Both storage owners are now constructed before cache
construction completes. Manifest loading and pack opening remain lazy; the
constructor introduces no file load or directory scan. The fixture uses actual
production owner/accessor/constructor/destructor code, real static destruction
and real jthreads. Baseline execution reports four lifetime failures; fixed
execution proves both owners survive worker shutdown/final flush and that
manifest loading happens only once on demand. The inherited portion is included
explicitly because leaving it would keep the corrected shutdown path unsafe.

### Shared API, UI and SE preservation

**Command retention was not bounded when the oldest command was pending.**
Completed records accumulated behind that reservation, and new pending work
had no capacity admission check. Admission now reserves space under the same
mutex as the command journal. It evicts the oldest completed record when
needed, preserves all pending identities, and returns retryable `service_busy`
when every slot is occupied by in-progress work. Duplicate, conflict and replay
handling occurs first. The test keeps an older command pending across 64
completed requests, fills all pending slots, and verifies rejection, retry,
identity retention and replay without timing-dependent sleeps.

**Persistent navigation retained borrowed translated strings.** `Section`
labels are valid for the current draw, while `Navigation` survives language
changes. `I18n::SetLocale`/`Reload` invalidates returned text pointers; later
developer inspection could dereference freed titles or disabled reasons for
pages not redrawn. Persistent metadata now owns its necessary strings and
reuses vector/string capacity during ordinary frames. The actual publication,
Select/Navigate and Describe paths pass mutation/free, republish, visibility,
transaction and live-profiling guard checks. The baseline fails the borrowed
buffer case. Address/undefined-behavior instrumentation also passed with leak
checking disabled because the container cannot support LeakSanitizer's thread
inspection.

**The editor lost SE localization boundaries.** Existing controls,
notifications, preview/undo text and water diagnostics are restored through
the shared display-label translation boundary. Stable JSON, search, undo and
category identities remain untranslated. This also corrects a comparison of
a localized category against an English internal value. Existing matching SE
translations are recovered for restored keys; new text keeps English fallback.

The boot-disabled feature message now directs users to **Advanced > Startup,
save and restart**, matching the new UI. The former instruction referred to
an enable control that was disabled in that state. New render-target timing
headings and coverage help now use localization as well. Touched unformatted
text calls preserve safe format handling.

IBL's measurement guard had also translated internal registry identifiers,
causing weather ownership checks to fail in translated locales. It now queries
stable `ImageBasedLighting` / `EnableIBL` identifiers and translates only the
displayed reason. General weather controls use the same authoritative manager
ownership state, which includes validated transition-source/target variables
and pause/release behavior, without reparsing JSON for each control.

### Validation gaps repaired

-   The expanded actor-bounds fixture was missing from the portable harness.
    It now compiles the complete production helper implementation while
    preserving the Windows host extractor interface. Current code passes 201
    checks in nine groups; the actual pre-port SE implementation fails 64 of
    those checks. This demonstrates coverage of port behavior, not a newly
    discovered actor runtime defect.
-   The D3D profiler mock ignored its output byte count. Optimized Release
    compilation exposed a possible 16-byte disjoint result copy into an 8-byte
    timestamp buffer. The mock now checks pointer and exact size before copying.
    New cases cover undersized, oversized, missing and valid destinations and
    adjacent-memory preservation. Native profiler code is unchanged.

## Scope, performance and duplication conclusions

SE's frame-generation providers, native DX12 presentation/pacing, retained FG
resources, HDR composition, fonts/cursors and performance-measurement protocol
remain authoritative. NR remains mutually exclusive with configured or live
FG. FOV-dependent behavior uses native mono projection and viewport data;
render dimensions follow SE arithmetic and output ownership.

The review did not import headset submission, stereo packing, controller
overlays or VR-only hooks. The source's raw cull-pool and global texture-list/
FaceGen assignment offsets remain excluded because a verified SE/AE ABI is
not available. These deliberate scope boundaries are recorded in
[main-se-port.md](main-se-port.md); they are not represented as implemented
native SE facilities.

The inspected grass/HiZ, culling/hysteresis, streaming retirement, scene
inventory and diagnostics paths produced no additional confirmed runtime
finding. Cache write queues remain bounded, publication checks generations,
and reviewed lock boundaries avoid holding conflicting locks across waits.
No speculative caching of swap-chain views or broad architecture rewrite was
introduced.

Performance corrections remove work on stale HDR content, suppress repeated
failed shadow allocations and bound API journal work/memory. Shared math and
texture-view resolution remove concrete duplication. Navigation reuses
allocated capacity. These are code-level properties; **no GPU speedup, FPS
gain or in-game performance equivalence has been measured**.

## Final verification

Final build counts, regression results and the supplemental shader diagnostic
are recorded in [main-se-validation.md](main-se-validation.md). The repository
contains the new regression sources and harness registration; scratch logs
alone are not the only reproduction path.

The normal Windows/MSVC and DevBench builds, native FXC/DXBC shader suite,
D3D11 WARP checks, real NR/vendor execution, SE/AE gameplay and HDR/FG visual
validation remain required. The actual native configure attempted here stops
before compilation because Visual Studio 18 2026 is unavailable on Linux.
Portable extraction and Linux DXC diagnostics do not establish a built plugin
or validated D3D11 production bytecode.

All confirmed findings above are addressed in the follow-up. This does not
assert that unexecuted native and game paths are defect-free. The unavailable
`CSX-perf-handover-20261029.zip` was not used as evidence.
