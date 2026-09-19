# Flat character Neural Rendering, 2026-09-19

## Supported configuration

On SE/AE select Full resolution, leave Restrict to FOV mask off, and enable
Characters only. Faces, Skin and Hair each retain their category switch and
strength. Adaptive eligibility, distance/depth tests, edge feathering,
diagnostics and experimental Multi-ROI use the existing mono-capable path.
Multi-ROI still supports at most two independent regions per view and keeps
its existing savings policy; this change makes no new performance claim.

Full-resolution NR runs on the final scene before UI. All shared colour
modes and Preserve source/Lighting preservation remain in the same logical
filter domain. Foveated and Reduced resolution remain VR-only. Stereo
submission controls are hidden on flat, and character previews/DevBench
report a single view. Saved stereo preferences are retained but ignored by
the mono provider path. The master-toggle correction remains intact.

## Implementation and contracts

The deferred MASKS2 attachment is RG16 UNORM on all runtimes. Its first
channel retains 16-bit inverse vertex AO; its second carries the existing
exact face/skin/hair/excluded codes. This adds two bytes per allocated
MASKS2 pixel on flat, including when NR is off, avoiding a target recreation
when character selection changes. Existing named target allocation and
failure handling are reused.

Lighting writes actor categories on flat as well as VR. Grass, distant
trees, effects and sky explicitly write no character category when drawn
in the deferred pass, using the source opacity required by inherited MRT
blending. Grass now supplies that opacity on flat as it already did in VR.
These writes avoid inheriting a character tag from a surface behind them.
Forward/depth outputs and the VR category encoding remain unchanged.
Deferred compositing reads inverse AO from the shared tuple's first channel.

The existing universal Lighting SetupGeometry hook classifies non-player
actors. Blended/ambiguous materials remain rejected; alpha-tested opaque
hair remains eligible. Player surfaces remain excluded, including in flat
third-person view. This does not add support for arbitrary unclassified
clothing or material types.

The existing flat framebuffer cache, one-view dimensions and source-frame
category/depth capture feed the shared CharacterRendering implementation.
Provider auto-mask remains enabled; the exact CSX mask is used only for
final output isolation, never passed as an undocumented ControlMask.
Mono Multi-ROI uses logical slot 0 and independent physical slots 0/4.
Private outputs, disjoint ownership and colour configuration latching are
unchanged. No semantic dilation, crop/jitter, history-reset or context
padding policy was altered.

DevBench runtimeSupport now advertises character support on SE/AE and
stereo scheduling only on VR. Its description and output schema document
one-view character diagnostics. Existing character configuration fields
work without a new API version or defaults.

## Validation

Route-policy tests cover flat Full resolution and reject flat B/C routes.
The extracted UI regression matrix now verifies enabling flat characters
without FOV and retains the always-editable master cases. Colour transaction
tests now cover mono full-scene, one-region and two-region character work
(39 total CPU transactions), with shared frozen Lighting preservation and
correct mono physical slots. Existing synthetic character-mask tests cover
mono/stereo extents, AO precision, category strengths, output isolation,
depth/crop/jitter and independent regions.

Passed source-only validation:

-   `pwsh -File ./tools/cmake.ps1 -P tests/neural_rendering_devbench_contract_test.cmake`
-   `pwsh -File ./tools/cmake.ps1 -P tests/neural_multi_roi_contract_test.cmake`
-   `python tests/neural_color/source_contract_test.py`: 8 tests passed.
-   `pwsh -File ./tools/cmake.ps1 -DPROJECT_ROOT=. -DOUTPUT_DIRECTORY=build/validation/nr-flat-character/ui -P tests/extract_neural_rendering_ui.cmake`
-   `pwsh -File ./tools/cmake.ps1 -DPROJECT_ROOT=. -DOUTPUT_DIRECTORY=build/validation/nr-flat-character/colour -P tests/extract_neural_color_route_latch.cmake`
-   `pwsh -File ./tools/generate-unified-presets.ps1 -Check`: preset values
    unchanged; required source fingerprints refreshed.
-   Scoped pre-commit checks and `pwsh -File ./tools/git.ps1 diff --check`.
    Clang-format 22.1.4 was applied and checked separately on changed ranges
    in Upscaling/Deferred and complete remaining changed code files. The
    full-file hook was skipped to avoid unrelated Upscaling macro churn.

The Multi-ROI source check's stale grouped-tooltip expectations were updated
to the current individual-control help, including its per-view limit and
VRAM/performance caveat. Algorithm and provider safety assertions remain.

No DLL build, shader compilation, game deployment or AIO was requested for
this extension. Compiled policy/UI/colour tests, existing character-mask GPU
tests, flat/VR shader permutations and live SE/AE/VR visual validation remain
to be run before claiming runtime qualification. The production AIO built
from `153af7cbc` does not contain this flat extension.
