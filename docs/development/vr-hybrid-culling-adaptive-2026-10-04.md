# Hi-Z finer-depth iteration: 4 October 2026

The nearest-vertex build still trails Advanced in the latest repeated
noon comparison: 6.7% more GPU time and 7.9% more CPU time.
The next implementation preserves 2x2 source depth instead of 4x4 where
the existing resource limits allow it. Its in-game benefit is unmeasured.

## Final same-build comparison

Two 20-second fpsVR windows per method ran in Advanced / Hybrid / Hybrid /
Advanced order at WhiterunExterior01, with SkyrimClearTU weather.
Noon was reset and verified before each window, with five seconds of
settling. Player position stayed fixed. DLSS Quality K remained at
1344x1492 render pixels and 2016x2240 display pixels per eye.
Boundary HMD observations differed by at most 0.507 mm and 0.0494 degrees;
these do not certify continuous pose stability.

| Mode     | Window                | Samples | CPU mean ms | GPU mean ms |
| -------- | --------------------- | ------: | ----------: | ----------: |
| Advanced | witness-advanced1     |    1556 |   10.704627 |    8.822943 |
| Hybrid   | witness-hybrid1-ready |    1195 |   12.106360 |    9.606946 |
| Hybrid   | witness-hybrid2       |    1195 |   11.482845 |    9.556234 |
| Advanced | witness-advanced2     |    1403 |   11.165574 |    9.137847 |

Equal-window averages are **10.935101 ms CPU / 8.980395 ms GPU** for
Advanced and **11.794603 / 9.581590 ms** for Hybrid. The differences are
**0.859502 ms CPU (+7.9%) and 0.601195 ms GPU (+6.7%)**.
The Advanced GPU baseline drifted by 0.315 ms, and Hybrid CPU repeats
differed by 0.624 ms. Both Hybrid GPU windows still exceed both Advanced
windows; this is not parity. These observations do not isolate a
cross-build speedup from the preceding campaign.

Relative to the campaign's nominal 120 Hz / 8.333 ms budget, those
differences are 10.3% CPU and 7.2% GPU. The quiet-window CSV does not
independently report refresh rate. CPU and GPU durations overlap and
must not be added.

Telemetry, traversal diagnostics and profiling were off during timing.
All four windows retained covering monotonic fpsVR records, unchanged
runtime/renderer identity, no shader failures and no detected compiler
activity. Each owned fpsVR logger was stopped and verified inactive.
The first Hybrid selection caused a subsequent setup command to exceed
its five-second main-thread deadline. It recovered before any Hybrid
timing began; that setup is excluded. No pictures were taken.

## Rejection and GPU cost

Separate 20-second counter windows, each reset and settled at noon:

| Mode     | Window                 | Culled records | Tested records | Rejection | Batches |
| -------- | ---------------------- | -------------: | -------------: | --------: | ------: |
| Advanced | witness-advanced-count |      3,361,719 |      5,976,064 |    56.25% |   1,459 |
| Hybrid   | witness-hybrid-count1  |      1,612,586 |      4,874,240 |    33.08% |   1,190 |
| Hybrid   | witness-hybrid-count2  |      1,626,438 |      4,907,008 |    33.15% |   1,198 |

Both Hybrid windows have zero fallback, invalidated and unreadable
batches. They average 4,096 tested candidates per batch, the native
capacity. These are repeated candidate records, not unique objects or
matched persistent cohorts. They do not quantify how rejection or
candidate capacity affects total frame time.

Two normal-shader Hybrid captures and one Advanced capture each resolved
300 GPU frames, with telemetry on and traversal diagnostics off.

| Hybrid GPU self time        | Capture 1 ms | Capture 2 ms |
| --------------------------- | -----------: | -----------: |
| Build base                  |     0.022799 |     0.024247 |
| Reduce mips                 |     0.024733 |     0.029652 |
| Hierarchy parent remainder  |     0.000651 |     0.000650 |
| Test bounds                 |     0.530623 |     0.532511 |
| Copy results                |     0.006045 |     0.005621 |
| Visibility parent remainder |     0.005300 |     0.005295 |
| Sum of disjoint self times  |     0.590151 |     0.597976 |

Bounds testing accounts for **89.1-89.9%** of measured Hybrid GPU work.
Base and mip construction cost only 0.048-0.054 ms. Parent rows are
exclusive remainders. These captures are separate from fpsVR timing;
subtracting the values does not predict parity. Advanced has no
equivalent native depth-culling GPU scope in this build.

Counter-window CPU means are 19.01-19.06 microseconds for dispatch,
2.87-2.98 for preparation, 10.16-10.42 for post-native history validation,
and 2.90-3.04 for intercepted native readback. These inclusive stages
must not be summed. They do not indicate a readback-stall explanation.

The direct connector still omits
`set_depth_culling_traversal_diagnostics_enabled`, although the DLL
reports diagnostic resources ready. No dispatcher or transport bypass
was used. Nearest-unresolved, finest-unresolved and depth-budget
proportions remain unmeasured. Existing local feedback
`AUTO-20261004-013128725-B1AC7F0D` now includes this build.
The recovered setup timeout is recorded as
`AUTO-20261004-055542612-56B125FA`.

## Implemented next experiment

Use the existing generic depth builder with a preferred source reduction
of two. If that layout exceeds the existing 4096-texel pyramid limit,
retry reduction four through the same validated layout helper. This
preserves admission of source dimensions up to 16384 and the 12-mip
limit. Invalid input still fails admission; allocation failure retains
the native fallback. No new shader, resource type or setting is added.

At the measured resolution, the stereo hierarchy changes from 512x512
with nine mips to 1024x1024 with ten. Logical uncompressed storage grows
from 2,796,192 to 11,184,800 bytes, an additional 8 MiB. This is not a
driver VRAM measurement. The existing DevBench source snapshot reports
the selected reduction, dimensions, mip count and logical bytes.

A 4x4 leaf merges sixteen source pixels, so one unrelated far or masked
pixel can defeat the complete leaf proof. A 2x2 leaf localizes that
uncertainty. Coarser proofs remain available; the extra level is visited
only when refinement requires it. The finest-cell vertex precheck also
uses finer depth. Both-eye agreement, guards, bias, exact face tests and
the 64-actual-read budget remain unchanged.

This is a rejection-efficiency experiment, not a measured speedup.
More detail can require extra traversal/face work, and the fixed budget
can retain objects that a different traversal would reject. No monotonic
rejection or timing improvement is promised. The small hierarchy cost
and large rejection gap justify this bounded test; they do not prove
coarse reduction is the dominant cause.

## Validation and review

-   `pwsh ./tools/cmake.ps1 --build D:/Coding/GitHub/skyrim-community-shaders/build/ahiz --config Release --target vr_hybrid_culling_policy_test vr_hybrid_culling_shader_test --parallel 4`: passed.
-   `ctest --test-dir D:/Coding/GitHub/skyrim-community-shaders/build/ahiz -C Release -R '(DepthCulling|VRHybridCulling|D3DContextProtection)' --output-on-failure`: 12/12 passed in 12.86 seconds; WARP 12.63 seconds.
-   Policy tests cover the measured layout, the 8192/8193 size boundary,
    maximum admitted source dimensions and malformed rectangles.
-   WARP demonstrates a hidden box retained by 4x4 reduction and culled by
    2x2, with an independent complete source-pixel proof. Covered holes,
    masks and invalid depth in either eye retain visibility.
-   Finer-depth tests include odd-edge padding, single-texel mip axes,
    bias, perspective, source-pixel and independent 3D ray oracles.
    Standard and reversed test orderings pass with production/diagnostic
    visibility agreement and no diagnostic UAV in production.
-   The source-pixel fixture was generalized for finer cells: omit empty
    guarded rectangles and keep holes inside its tested domain. Its
    original 4x4 coverage remains unchanged.
-   `pwsh ./build/astra-validation/adaptive/validate-fine-production.ps1`:
    actual compiler flags and forced-header audit, syntax and diagnostic
    absence checks pass for Hybrid, Temporal and Menu bridge. No separately
    linked production DLL or SE/AE runtime was exercised.
-   Source review verifies reuse of layout validation, unchanged resource
    ceilings, packed traversal limits, native fallback and VR-only admission.
    The shaders and diagnostic schemas are unchanged.

Advanced remains default. Motion and lifecycle qualification remain open.
The new build needs another controlled Advanced/Hybrid comparison before
a performance claim or pictures.

## Evidence identity

Measured clean source: `ac2b7dcfc6f67d8c8f2230bcc5dd0f408d03f4c4`.
Producer Build ID:
`a166eba911d48a6ed16b7a2c624e943442f5a9ef775da0685c0cebc2663b887b`.

The physical enabled DLL, adjacent manifest, AIO receipt and six depth
shaders matched that source. No competing enabled loose, Overwrite or
unmanaged Data provider was found at those paths.

Raw results, CSVs, counter windows, profiler histories and restoration
receipts remain in main-workspace
`build/astra-runtime/20261004T054819Z-hiz-witness/`.
Production checks are under worktree
`build/astra-validation/adaptive/production-20261004T060249506Z/`;
the final focused test log is `build/astra-validation/adaptive/fine-depth-tests.log`.

Advanced and the original telemetry preference were restored with
profiling inactive. The engine's `qqq` command closed PID 27800;
the 06:00 UTC process check found no Skyrim process and no active
RootBuilder deployment. Implementation began only after that proof.
The next universal DevBench AIO is built by `build-fine-depth-aio.ps1`;
its receipt records source, package validation and delivery.
