# Early NR mask bounds, savings gate and area audit

The savings gate is now independently switchable in Character Rendering
Advanced and through `nr_configure.experimentalMultiRoiSavingsGate`.
It defaults on and is session only. Turning it off admits any strictly
smaller split; coverage, disjointness, dimensions and history ownership
checks remain mandatory. It does not enable experimental Multi-ROI itself.

Early GPU category-mask reduction now supplies current-source ROI bounds
when its nonblocking readback is ready. Geometry is the fallback for missing
GPU evidence, not a constraint on ready mask-derived splits. The cost
comparison uses the actual padded, stabilized single-ROI fallback from the
active planner. Previously the planner rebuilt a
fresh enclosure, which could be smaller than the fallback actually used.
The current-frame cache includes both that rectangle and the gate state.
Menu and DevBench changes share the existing history transition; changing
the cost gate alone does not retire a healthy backend.

## Captured-scene arithmetic

Evidence comes from `roi-performance-f1ae5f2aa-20260915T195300Z`, under
`build/validation/nr-live-20260915`, source
`f1ae5f2aa97ecb500db3748c053b221ea442689f`, producer Build ID
`91e55daa639e8a47d9db7a00edc71b3e13998f9277e500205bbccf2a83b68ee4`.
Each character case retained one coherent prepared stereo observation.
The inference GPU sample history independently confirmed one evaluation
per eye throughout the measured windows. Cleared main-thread snapshots
are not evidence of empty masks or absent characters.

The two actor eligibility rectangles were uncompacted. Production builds
both these rectangles and actor planner bounds from the same `local`
rectangle. Replaying their coordinates therefore reproduces the fresh
split candidates, but does not reconstruct an unobserved history envelope.
The faces snapshot is source frame 36633; all categories is frame 38843.
Each eye is 1512 x 1680 pixels. All figures below include provider padding.

| Case / eye               | Actual single pixels | Fresh single pixels | Candidate sum | Difference: single minus split | Overlap |
| ------------------------ | -------------------: | ------------------: | ------------: | -----------------------------: | ------- |
| Faces / left             |              180,224 |             163,840 |       147,456 |                32,768 (18.18%) | No      |
| Faces / right            |              180,224 |             163,840 |       131,072 |                49,152 (27.27%) | No      |
| Face, skin, hair / left  |            1,040,000 |           1,040,000 |     1,135,232 |                        -95,232 | Yes     |
| Face, skin, hair / right |            1,106,560 |           1,106,560 |     1,135,232 |                        -28,672 | Yes     |

With the gate enabled, entry requires
`savedPixels >= 65536 + ceil(singlePixels / 4)` per eye; retention uses
`ceil(singlePixels / 5)`. For either face eye, the entry threshold is
110,592 pixels, or 61.36% of the actual fallback. The fixed reserve makes
this substantially stricter than a plain 25% rule at small ROI sizes.
The reserve is a heuristic, not measured invocation cost.

Disabling the gate admits the replayed face candidates: 278,528 stereo
pixels instead of 360,448, a 22.73% reduction. That is 5.48% of the stereo
image instead of 7.09%. These are geometry-fallback arithmetic replay results, not new-DLL
performance measurements or predictions of the primary GPU-mask path. The previous GPU-mask implementation reached
a smaller median 4.60% footprint in its measured face-multi window.

All-category eligibility rectangles already overlap before padding by
88 horizontal pixels on the left and 84 on the right. Disabling a cost
gate cannot make those bounds disjoint. The previous synchronous GPU
mask reduction could identify empty space inside conservative projected
bounds. Replacing that reduction with geometry eliminated readback waits
but lost the tighter semantic-mask bounds. This is an algorithm tradeoff,
not evidence that the selected category tags disappeared.

## Area versus GPU cost

The same-build repeat at FOV 0.95 measured these median Feature 18 GPU
times with two visible characters and a fresh CSXTest01 load per case:

| Configuration                | Evaluated stereo area | Feature 18 GPU time |
| ---------------------------- | --------------------: | ------------------: |
| Full screen NR               |                  100% |          24.3515 ms |
| Faces, single ROI            |                 7.09% |           6.1835 ms |
| Face, skin, hair, single ROI |                42.25% |          12.0820 ms |

Face-only NR costs about 25.4% of full-screen NR while requesting 7.09%
of its area. Area savings therefore do not translate proportionally to
GPU savings. Region instances have separate histories but execute on the
same D3D12 command list/queue, so their work accumulates. The current
interface creates features against full-capacity textures and submits
subrect coordinates; it does not create compact cropped tensors for each
ROI. Padding, fixed work and dispatch/occupancy effects are plausible
contributors, not an identified breakdown of NVIDIA's private runtime.

The GPU timestamps bracket the Feature 18 execution loop, after input
preparation and resource transitions, and end before output transport.
The removed D3D11 mask-bounds readback wait is not inside this timer.
Whole-frame time additionally includes baseline DLSS, mask preparation,
color transport, compositing and synchronization. Do not add the old
readback wait to GPU inference time as independent frame costs.

GPU semantic-mask reduction remains the tighter way to find bounds.
The current NR interface takes those bounds as CPU parameter values.
The early nonblocking reduction is now implemented at the post-terrain
category capture hook. A three-entry ring preserves current-source
identity and optional ready results are consumed before inference.
The live comparison must measure its availability and cost. GPU-only
packing would need new color/depth/motion-vector and temporal-history
contracts; it is not part of this change.

## Diagnostics and validation scope

`multiRoiDiagnostics` exposes the actual fallback, candidate rectangles,
exact integer costs, overlap/coverage results, active gate, and candidate
rejection counts. Cost success alone does not admit a geometrically
unsafe split. Unavailable or overflowing costs are null; nonpositive
savings have `positiveSavings=false`. Candidate selection and count
semantics are specified in `dlss5-character-neural-rendering.md`.

The focused C++ tests include both captured stereo fixtures, exact
one-pixel entry/retention thresholds and ceiling division, sum overflow,
same-frame gate/baseline changes, invalid baselines, coverage bridges,
overlap with the gate disabled, and adversarial motion under both policies.
Settings tests cover default-on state, round trips and transactional type
rejection. Additional tests cover early category reduction for all eight
selection combinations, both stereo strides, excluded materials and
capture validity, nonblocking reads behind an actual GPU queue gate, and
conservative crop/jitter/feather mapping against every possible source tap. Source contract checks cover the menu, settings keys, runtime
plumbing, DevBench schema and retained nonblocking preparation.

The independent coordinate audit is retained locally as
`roi-savings-gate/captured-area-audit.json`, with source response hashes.
Its arithmetic assertions and both CMake source-contract checks passed
before the implementation commit. Compilation and executable test results
are recorded with the subsequent AIO build receipt. New-DLL live scheduling availability, timing and physical HMD
presentation remain unverified by these tests. The implementation uses
this branch's existing `CS_PROFILE_SCOPE` GPU profiler instrument; the
newer repository-wide `CS_GPU_PASS` macro is absent from this worktree.
