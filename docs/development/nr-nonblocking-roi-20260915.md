# Nonblocking character NR planning, 15 September 2026

The live comparison exposed a render-thread synchronization cost in
experimental multi-ROI: the GPU mask reduction required completion of earlier
rendering before CPU rectangle planning could proceed. Independent NR handles
and histories do not imply independent GPU queues or independent readback.

## Implementation

The active planner now uses the existing conservative projected actor bounds
for the current source frame. Each eye can retain up to two independent dense
NR regions, provided every current actor and compacted eligibility rectangle
is covered, padded regions are disjoint, and the savings gate passes. The gate
charges a 65,536-pixel reserve for the extra invocation before requiring 25%
net area savings, or 20% when retaining a split. This is a conservative
heuristic; it is not calibrated GPU break-even or a performance guarantee.

The rendering path no longer allocates GPU mask-bounds resources or dispatches,
copies, flushes, or waits for that reduction. The precise R8 face/skin/hair mask
still controls compositing. Delayed coverage diagnostics retain nonblocking
polling and cannot exclude new geometry or bypass current evaluation. Current
CPU-proven empty eyes can still bypass NR. The complete stereo batch retains
source-frame, depth, generation, dimensions and policy validation before its
results are published. Retained-menu source identity and per-region history
ownership remain explicit.

DevBench and the menu identify geometry planning and the extra-call reserve.
Legacy mask-bounds fields remain present as unavailable evidence (false/zero,
invalid rectangle, no failure frame), rather than reporting geometry as a GPU
mask measurement. This shared-code change adds no SE/AE rendering route and
does not qualify physical headset presentation.

## Preserved old-build comparison

Source: `6b23081aaa0b663bc1a65836bf1457aaf2f26c5c`.
Producer Build ID:
`641c7f5571a8f47f3fb46130e6cffa9a5fa1f06c09a5a7c687a5e77e57bd68b7`.
Evidence under the workspace:
`build/validation/nr-live-20260915/roi-performance-20260915T185130Z/`.
`REPORT.md`, `native-analysis.json`, `comparison.csv`, the raw r3 responses and
original stereo images preserve settings, every retained sample and exclusions.

Each case loaded `CSXTest01` freshly and began measurement immediately after
load/menu barriers and configuration. Each retained 40 health/NR observations,
excluding the first five as warmup. Enabled cases supplied 24–25 unique GPU
samples covering both eyes and the prepared physical regions. Both characters
were visible in each post-sample stereo capture; no movie proves every frame.
The Valve null driver used 1512 by 1680 pixels per eye, final-LDR insertion,
FOV-only scale 0.95, identical colour settings and diagnostic views disabled.

| Case                       | Median inference area | Median NR GPU ms, both eyes | Observed engine FPS |
| -------------------------- | --------------------: | --------------------------: | ------------------: |
| NR off                     |                    0% |                           0 |               60.49 |
| Full-screen NR             |                  100% |                      24.247 |               22.38 |
| Faces, single ROI          |                 7.42% |                       6.156 |               32.37 |
| Faces, multi-ROI           |                 4.60% |                       5.742 |               28.63 |
| Face/skin/hair, single ROI |                40.94% |                      11.570 |               27.94 |
| Face/skin/hair, multi-ROI  |                 9.67% |                      10.746 |               26.35 |

Area is evaluated NR pixels divided by 5,080,320 stereo pixels, not the fraction
visibly replaced by the character mask. The full-screen route evaluated 100%
despite the FOV setting. NR-off stale GPU results were excluded. Face multi-ROI
used two or three physical evaluations; face/skin/hair mostly used four. Mean
summed readback waits were 10.16885 and 11.63639 ms, respectively. Single ROI
did not execute this readback path. No NR, stereo, or readback-fallback counter
increments occurred in these final windows.

These are single immediate-post-load runs with mod startup, moving actors and
common polling overhead. FPS derives from engine-frame deltas and scenario
elapsed time, not headset delivery or steady-state production pacing. Native
GPU timestamps measure NR, not the complete GPU frame. Earlier r1/r2 attempts
and the generic profiler semantic-adapter rejection remain in the evidence;
they are excluded from the table. The active recording was stopped and the
profiler disabled. This evidence establishes the old synchronization cost; it
does not establish the new DLL's performance or image quality.

## Validation scope

The implementation adds regressions for marginal split costs, entry/retention
hysteresis, undersized area arithmetic, and same-frame newly visible actors.
The existing 4,000 adversarial moving-actor cases and maximum-dimension cases
retain coverage/disjointness/history checks. Integration contracts reject a
return of bounds waits/flushes and preserve asynchronous diagnostic polling,
current CPU-empty bypass, stereo identity and DevBench semantics.

Build and test receipts belong under
`build/validation/nr-live-20260915/nonblocking-roi/`. Required checks are the
Release universal plugin build, character policy/mask GPU tests, NR integration
contracts, six colour assets and the 16 standalone neural-colour tests. The
implementation is committed before compilation; receipts identify the exact
compiled commit and test results separately.

The updated DLL still requires a fresh-load comparison of all six cases, with
two characters verified, and motion/category/split-merge/insertion checks.
Geometry may require larger rectangles or one enclosing region; removing the
wait does not by itself prove lower frame time. No additional old-build run,
30-minute recording, AIO validation campaign or live DLL replacement is needed
to prepare this candidate.
