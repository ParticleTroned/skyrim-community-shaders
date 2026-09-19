# Neural Rendering route port

The working branch starts at `60179f5b5` and integrates the source history
through `d69bdb7eb`. The renderer keeps the source managed colour pipeline,
character classification and exact-mask composition. The separate Neural
Rendering feature owns the master switch, route, execution controls,
character controls and colour configuration. Upscaling retains a narrow
renderer adapter and migration reader; its settings writer excludes NR keys.

## Routes and image domains

| Mode                       | Model input and placement                                       | FOV restriction                      | Character selection                        |
| -------------------------- | --------------------------------------------------------------- | ------------------------------------ | ------------------------------------------ |
| Full resolution            | Final scene at output resolution, before UI                     | Optional, shared FOV geometry        | Exact authored mask and bounded ROIs       |
| Foveated                   | Existing vendor centre; selectable centre/final-scene insertion | Existing FOV pipeline                | Exact authored mask and bounded ROIs       |
| Renderscale NR before DLSS | Render-resolution NR, followed by DLSS                          | Automatic in VR; full image on SE/AE | Selection before DLSS at render resolution |

Full resolution prepares depth and motion guides independently from vendor
upscaling and the FOV toggle. The full-image blend explicitly covers the
square image, including its corners; an ellipse with radius one is not a
full-image mask. VR submit ownership stays with the existing presentation
pipeline. SE/AE supports Full resolution and Reduced resolution, both with
shared character selection; VR supports all three routes. Foveated rendering
and FOV restriction remain VR-only. A saved unavailable FOV restriction
keeps NR pending and removable without disabling the master switch.
Unsupported enabled rendering modes are rejected with a capability explanation.

The [flat reduced-resolution adapter](nr-flat-reduced-resolution-20260919.md)
uses the active mono render extent, then feeds the complete private result
into ordinary DLSS. Failed preparation or inference leaves DLSS on the
original scene. This adds no independent model downscale or alternate
character/colour algorithm.

In VR, [Renderscale NR before DLSS](nr-renderscale-fov-20260919.md) uses the
same configured crop, eye offsets and outer mask composition as Foveated.
Both require enabled FOV; the saved `fovOnly` preference applies only to
Full resolution there. Switching modes preserves that preference. An
unavailable mask pauses execution without locking the master switch.

Reduced resolution uses Feature 18 at 1:1: colour, guide, output, and both
sides of its crop transform describe the render-resolution domain. Feature
18 resets every frame because the private provider ABI has no evidenced
jitter/camera history contract. DLSS remains the temporal reconstruction
owner. Dedicated Vincent colour treatment is intentionally deferred; every
mode uses the same colour-managed source implementation.

## Character ROI composition

The source category-authoring hooks, early post-terrain category/depth
capture, same-source-frame evidence, conservative actor fallback, ROI
savings gate, up to two disjoint regions per eye, and eight physical provider
slots remain connected. The exact character mask is a CSX compositing input;
it is not passed as an undocumented provider ControlMask. Provider auto-mask
remains enabled.

For reduced resolution, projection and mask extents use the render domain.
After successful inference, each private candidate starts with a complete
copy of the original low-resolution image. Only validated produced regions
are composited, using the exact character mask when enabled. Mono requires
one complete candidate; VR requires both before either DLSS eye consumes
NR. Undefined gaps between disjoint ROIs are neither copied nor sampled.
A candidate failure retains the original input for the complete mono/stereo
transaction. Empty character views bypass inference while remaining part
of that coherent image transaction.

Both flat routes use one required view; VR requires two. Character selection,
private outputs and source colour/Lighting reconstruction share the same
transaction machinery across the supported routes. SE/AE and VR share the
RG16 MASKS2 tuple: 16-bit inverse vertex AO in the first channel and authored
face/skin/hair category tags in the second.

## Problems found and corrected during integration

-   The source OpenVR submit hook signature predates the target ABI. The port
    retains the target native-boundary signature and freshness authority.
-   A source texture/cycle match could admit cached NR before target producer
    proof. Reuse now also requires the target producer proof and retained
    colour/depth/motion ownership; stale or unproven pairs fail closed.
-   Source boolean vendor returns would erase the target FSR `Deferred`
    result. Foveated wrappers preserve the full vendor result.
-   The target spatial fast path could reevaluate an already prepared centre
    and replace its NR/character output. Prepared NR centres use exact-mask
    composition; disabled NR retains the target fast path.
-   Source compile-time arrangement and insertion assumptions excluded the
    reduced and independent full-image routes. Selection is now dynamic and
    participates in settings/history keys.
-   Source character projection used output resolution for the new reduced
    route. It now matches that route's render-resolution crop domain.
-   Full-mode raw guides are frozen before the engine replaces render-depth
    with upscaled depth; the pre-UI pass consumes that same-frame snapshot.
-   An inherited forced SSS hook installation was unnecessary because category
    authoring is integrated in the target shared hooks. The extra traversal
    while SSS/NR is disabled is removed.
-   Source forced FOV visualization off while loading settings. The target's
    saved visualization preference is preserved.
-   Upscaling defaults could clear the independent feature's configuration.
    Upscaling resets preserve NR settings; Neural Rendering owns its reset.
-   A merged generic colour-region transform inherited an eye-specific
    emergency fallback. The transform now reports failure without altering
    unrelated targets; the submit-eye wrapper retains the target fallback.
-   Target FPS Stabilizer comparisons use effective render-scale state rather
    than only the remembered preference. That distinction is preserved.
-   Main disabled NR avoids menu/temporal/HDR/route-publication work. Explicit
    capture evidence keeps its existing default-off atomic gate so NR-off
    baseline capture remains available.

## Validation boundaries

The branch implementation report records build and test commands and their
results. Static policy tests cover mode parsing, enforced image placement,
reduced-before-DLSS arrangement, and coherent mono/stereo success/bypass
masks. Source colour and character GPU tests cover their preserved graphics
contracts. Compilation alone does not establish runtime visual fidelity or
performance. Flat A/C with character selection, and VR A/B/C with character
selection and FOV restriction, require runtime evidence before making those
claims.
