# Character Neural Rendering on Gogh SE

The Upscaling menu provides **Full scene DLSS 5** and **Characters only**
under **Enable DLSS 5 Neural Rendering / Apply to**. Existing settings keep
full-scene rendering; NR remains disabled by default. Both modes use Gogh's
DLSS-then-NR order on SE and AE. This branch has no VR execution path.

Character mode enhances actor-owned NPC face, skin and optionally hair
materials. Player geometry, armor and unsupported or alpha-blended
materials are excluded. Opaque and alpha-tested hair are supported. Face
and skin selection default on; hair defaults off. Strengths are independent.
The distance limit defaults to 10 meters; zero removes that limit. Adaptive
selection, a minimum projected face size, region margins and selection hold
frames control which characters are evaluated. Advanced controls provide
optional split character groups, depth-aware feathering and previews of
the mask, selection bounds and NR output.

## Rendering and failure behavior

The unconditional lighting hook classifies actor ownership independently
of the Subsurface Scattering feature. Semantic categories occupy a separate
G-buffer channel in `R16G16_UNORM`, retaining the original 16-bit vertex AO
precision even with NR disabled. Blended draws cannot blend
category values into another valid character category. Opaque excluded
geometry remains excluded from feathering.

The deferred pass captures the categories and matching scene depth before
decals can change them. Mask preparation validates the world-frame identity,
render/output dimensions, resource generation and policy. A category selection
that exceeds the captured policy requires a new world capture. It combines
material identity, actor eligibility, captured jitter, visibility depth and
optional depth-aware feathering into an R8 mask. Retained paused-menu frames
can reuse only matching captured world data. Empty selection skips NR;
unavailable or mismatched capture falls back to ordinary DLSS. Asynchronous
coverage samples retain their source frame and content identity for diagnostics;
historical samples cannot establish that the current selection is empty.

A stable enclosing rectangle bounds NR work. Optional split selection uses
at most two non-overlapping groups with independent persistent histories.
All regions are validated before allocation or submission and published
only after all evaluations succeed. Mode, policy, extent and region-history
changes reset temporal state. No private provider control-mask ABI is used:
NR retains its automatic mask and CS composites the successful output.

The private output is initialized from the current DLSS image before partial
region evaluation. Baseline DLSS and NR are sharpened independently, then
composited with the exact selection mask. Zero-mask pixels preserve baseline
RGB and alpha, even beside an enhanced pixel or invalid NR value. A failed
NR evaluation, mask operation, shader compilation or composite leaves the
normal DLSS result available. Composition resources are checked before vendor
evaluation so unavailable composition does not repeatedly evaluate invisible
output. Clearing shaders invalidates compiled character
shaders, prepared masks and temporal state.

Settings are persisted as the nested `neuralCharacter` object in Upscaling
settings. The `enabled` field selects character mode. Defaults and finite
range checks are centralized in `CharacterSettings.h`. JSON counts and enums
are checked before integer narrowing; invalid types preserve the previous
settings. Configuration reload,
default restoration and menu edits invalidate affected state. Visibility
depth checks default on. The serialized mask test mode is a developer
validation option; normal operation uses `Authored`.

## Validation

Build the universal SE/AE DLL and host targets with the existing local
preset, without automatic deployment:

```powershell
pwsh ./tools/cmake.ps1 --build build/AIO-Release --config Release --target CommunityShaders run_host_tests character_mask_gpu_test --parallel
```

The host CMake directory registers standalone actor selection, region,
multi-region, work-rectangle, subrectangle mapping and settings tests. The
`character_mask_gpu` test uses D3D11 WARP to compile and execute the actual
capture, mask and composite shaders independently of the game or NVIDIA
runtime. It also compares vertex AO storage against the original R16 format
and tests subpixel coverage of nearby characters against distant background.
Settings tests exercise the actual JSON boundary, including large and negative
integers, invalid types and round trips. Build the test targets, then run:

```powershell
ctest --test-dir build/AIO-Release -C Release --output-on-failure -R "PerformanceTuningTests|character_|compute_subrect"
```

In-game validation must be performed on each supported SE/AE runtime:

1. Compare NR off, full scene and characters only in the same lit scene.
   Check NPC face, exposed skin, opaque/cutout hair, clothing, the player,
   overlapping actors, vegetation and foreground occluders. Repeat with
   Subsurface Scattering disabled at boot.
2. Toggle each category and vary its strength. Inspect mask and bounds
   previews while moving past the distance and face-size thresholds.
   Check temporal hold, optional feathering and split character groups.
3. Check an empty scene, separated groups, a character entering/exiting the
   frame and an occluded character. Surroundings must match normal DLSS;
   empty selection must bypass NR.
4. Exercise Native AA through Ultra Performance, window/resolution changes,
   shader reload, configuration reload and restoring defaults. Switch modes
   repeatedly and change DLSS/FSR/TAA selection.
5. Open dialogue, inventory, map, main/loading and Community Shaders menus;
   load a save and travel between cells. Inspect first resumed frames for
   stale masks or histories. Repeat with supported frame generation on/off.
6. Exercise missing/failed NR runtime and failed shader compilation. The
   scene must remain on normal DLSS, with failure diagnostics available.

Host and WARP checks do not replace visual SE/AE and vendor-runtime testing.
