# Hi-Z projected-face refinement: 4 October 2026

PR104 implements Hi-Z culling. PBR grass, grass optimizations and renderer
depth-convention changes remain separate later work. The PR description
contains the current implementation and final comparison status; detailed
historical evidence remains in the dated reports.

## Coverage and depth

The previous shader refined the projected rectangle but still compared
every cell against the nearest corner of the entire box. The current
shader keeps that inexpensive coarse proof, then tightens inconclusive
cells in finer grids using the projected box faces.

All eight corners are retained in eye-local pixel coordinates and z/w.
Each of the six faces is split into two triangles, without back-face
rejection. Each triangle is clipped against the depth cell's rectangle,
expanded by the configured pixel guard plus 1/32 pixel for clipping
roundoff. Cells with no intersecting face need no depth proof. Otherwise
the nearest depth of each clipped polygon must be behind the cell's
farthest scene depth. The configured depth bias includes an additional
64/16777216 allowance for interpolation. Extrema occur at clipped
vertices because projected depth is affine across each triangle.

The clipper has eight slots per polygon; four planes grow a triangle to
at most seven vertices. Invalid intersections or capacity failures cannot
prove occlusion. Degenerate faces are retained by clipping rather than
discarded by an area or winding threshold. Near/far crossings, invalid
projection data and uncovered viewport guards still retain visibility.
Both eyes must prove occlusion. Source reduction, masks, delayed history,
motion validation, native fallback and user settings are unchanged.

The four-load coarse proof runs first. Finer grids retain the 64-load
budget per eye, including the coarse four and every reserved grid. A grid
stops on its first unproven cell, but reserves its entire load count;
partial coverage cannot prove occlusion. Face clipping runs only when
the whole-box nearest depth cannot prove that cell hidden. A visible
face ends that cell's proof immediately. The smallest initial mip still
uses the coarse proof alone. Subpixel raster coverage and the guard,
source reduction and finite budget can still differ from native OBB
rasterization. Matching the native rejection percentage is not assumed.

## Validation

The Release test target built successfully. `VRHybridCullingShader`
passed 1/1 in 17.17 seconds, compiling and executing Standard and reversed
test orderings; runtime remains Standard Z. The previous shader fails
the new empty-corner regression. Scoped formatting hooks passed. Existing clip, mask, padding,
viewport, stereo, budget and source-pixel oracle cases remain enabled.
New fixtures require rejection of an occluded diamond with clear depth
in its empty rectangle corner, and of a sloped box whose local depth
passes while its global nearest depth fails. A hole or invalid pixel in
either eye retains visibility.

An independent double-precision oracle intersects rays with the 3D box
using inverse affine transforms and slab tests, without projected
triangles or polygon clipping. It checks 96 rotated/sloped boxes against
four depth fields, orthographic and perspective projections, asymmetric
eyes and nine guard-offset rays per pixel. Both depth orders exercise
the same oracle. This is sampled geometry evidence, not in-game motion
qualification.

Exact local commands and results are preserved under worktree
`build/astra-validation/hiz-faces/`. An intermediate boolean grid
accumulator lost a failing-cell result under optimized SM5 compilation;
the hole regression exposed it. The final integer failure count and
early loop termination retain those failures. No failed candidate was
packaged or deployed.

## Runtime evaluation

Existing DevBench-only batch/object counters, native counts, explicit
windows and GPU/CPU scopes measure this shader through the same backend.
No diagnostic resource, production instrumentation or logging increase
is added. Advanced remains default.

The [final projected-face comparison](vr-hybrid-culling-adaptive-2026-10-04.md)
found Hybrid at 15.59 ms CPU / 9.84 ms GPU versus Advanced at
11.54 / 8.16 ms and Legacy at 10.97 / 8.13 ms. Candidate rejection was
34.1% versus approximately 62% native. All observed Hybrid batches were
accepted, without fallback or invalidation. This shader remained slower;
no images were requested after that result. Motion and lifecycle
qualification remain open. The linked report describes the subsequent,
not yet measured adaptive traversal.

## Developer AIO

The universal Release DLL and DevBench ON / Tracy OFF AIO built and
passed archive integrity, staging inventory, shader and producer identity
verification. All 370 files matched by path, size and SHA-256, including
`ProjectedBounds.hlsli`. Source remained stable during packaging, and all
three preceding AIO archives were preserved.

-   Compiled source: `6423b3e8f03337324f560afd6b467ee1b1b709d2`, dirty.
-   Dirty digest: `c14f7ec67fa3f163da489283ce0c5c7b809270bec8912ee4cd82c69f8c5239f4`.
-   Build ID: `5285d5b836772a2745efd8a0db078bad59b8698fa69c52df3dcccf6a7522673c`.
-   [AIO](D:/Coding/GitHub/skyrim-community-shaders/dist/CSX_AIO-astra-hiz-faces-DevBench-5285d5b83677.7z).
-   [Verification receipt](D:/Coding/GitHub/skyrim-community-shaders/dist/CSX_AIO-astra-hiz-faces-DevBench-5285d5b83677.receipt.json).

Complete build and delivery evidence is under worktree
`build/astra-validation/hiz-faces-rebuild-20261003T232211751Z/`.
The installed package and producer identity were verified for the linked
in-game comparison. Later report updates do not replace its compiled identity.
