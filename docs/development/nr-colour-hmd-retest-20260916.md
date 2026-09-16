# Native HMD colour retest: camera repaired, baseline variation

The replacement camera observer passed live Skyrim VR capture. The final
menu-free, stationary scene still varied with NR off. No comparative NR
colour candidates were dispatched, and no colour correction or winning
mode is established. The automated gradient gate rejects these images;
that is not proof of character movement or a visible NR defect.

## Producer and preparation

Compiled source: `e3e731a83ff72387bdeebdae5b038cf87d826656`, clean.
Build ID: `4385d3607500f6dd1006d3c90bd9a15b89eb193ab3795b4300b9155ec37c38e9`.
CommunityShaders.dll: 24,922,112 bytes, SHA-256
`ae697555b7cb4e541dbb89428da89decb41c64f7aadf389e2d6105243130dfcb`.

Mapped-file inspection proved the exact enabled AIO provider for PID 28724,
started at 2026-09-16T10:10:58.7396299Z. The physical DLL, adjacent manifest
and AIO receipt matched. Exact profile-relative probes found one enabled
loose provider and no DLL in Overwrite or unmanaged Data. The separate
DevBench DLL was physically matched too. Final verification retained the
same process, source, Build ID, DLL hash/size and bridge-enabled manifest.

The user authorized this manually launched session. The legacy MO2 lease
was recorded without adoption or modification. Direct typed DevBench MCP
was the only live transport. No performance campaign was run.

Foveated dispatch was initially off. A short prerequisite probe confirmed
successful stereo inference after enabling it; this was not colour-quality
evidence. The user specified FOV 0.95 and no peripheral TAA. Each subsequent
plan pinned those settings, DLSS, native 2468 by 2740 eye images and
character face/skin isolation. The outdoor scene used the user's changed
render-scale/quality settings and therefore has its own frozen plan.
No cross-scene or cross-configuration NR ranking is claimed.

## Final stationary outdoor attempt

The latest evidence is
`build/validation/nr-hmd-outdoor-fixed-20260916T112605Z-precision`.
Three unchanged NR-off sequences produced 36 native stereo pairs.
A shared correlated recording covered the rapid capture block for
56,030 ms, without menu/activity events, truncation or an unrecorded tail.

Every pair passed source, committed artifact/hash, dimensions, eye identity,
configuration, camera and recording checks. Camera view and projection
matrices were identical. Maximum position-adjust drift was 0.00146484375
engine units, below the predeclared 0.015625 tolerance.

An earlier retry had used a 0.00001 position tolerance, below float32
precision at the approximately 18,825-unit coordinate. All its pairs
remained rejected. Before new capture, the final plan pinned an actual
previously acquired camera and a tolerance of eight float32 ULPs at that
coordinate. Regions and the 0.15 image-gradient threshold were unchanged.
No rejected image was relabeled as passing.

The final gradient-presence/sign gate rejected 323 of 432 comparisons.
These are image differences, not measured camera motion. Relative to the
first baseline image, the largest decreases in mean encoded luma were:

| Region          | Left eye, code values | Right eye, code values |
| --------------- | --------------------: | ---------------------: |
| Face            |               -6.2830 |                -6.3846 |
| Exposed thigh   |               -6.5796 |                -6.4563 |
| Armour          |               -6.6757 |                -6.4832 |
| Wood background |               -1.6573 |                -1.2930 |

Face gradient mismatch medians were 0.2773 left and 0.2785 right; thigh
medians were 0.6480 and 0.6340. A gradient mismatch is not an isolated
motion detector: illumination, reconstruction noise and fine texture can
also affect it. The current fixed gate does not qualify this baseline.

A fresh blinded reviewer inspected all 108 native crops and 18 overview
images, covering both eyes, six regions and ordinals 1, 6 and 12 of each
sequence. It found no obvious contour movement, changing occlusion,
camera drift or broad colour/exposure shift. Subtle skin-grain variation
was possible, with low confidence. This does not invalidate the measured
small code-value changes or establish their perceptual significance.

The reviewer also found that the planned `barrel_ring` crops mostly cover
dark wood and narrow metal edges. Native bright-reflection coverage is
therefore insufficient. Two attempted full native-eye reads failed in the
image viewer; full-native-eye inspection is not claimed. All four candidate
objectives remain indeterminate because only unchanged references were
supplied.

The user suggested flickering outdoor illumination. That is compatible
with variation already present with NR off, but the captures do not prove
its cause. Neither this suggestion nor the gradient result establishes an
NR defect, a model colour domain or a correction.

Actual within-sequence spacing was 522.679 to 599.453 ms. This sparse
sampling cannot qualify headset frame-rate flicker. Of 36 requested engine
exposure companions, 19 remained available and 17 returned
`evidence_retention_expired` after deferred batch retrieval. Available
observations reported ratio 1 and frame gamma exponent 1. They are engine
observations, not inference bindings or colour-domain proof. NR-off
per-eye inference bindings were unavailable as expected.

Canonical analysis imported three sequences and retained all 66 remaining
schedule steps as not run. Its 217 flagged regional rows use a different
reference from the pre-dispatch gate's 323 failures. The 89 unavailable
exposure observations comprise 72 per-eye inference observations and
17 expired engine companions. No normalization or image registration was
used.

## Preserved earlier attempts

Each row represents three 12-pair NR-off sequences. Rejected captures,
plans, recordings and failures remain separate and immutable.

| Evidence directory under `build/validation/`  | Result                                                                                                                               |
| --------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| `nr-hmd-retest-20260916T1015Z`                | 36 provenance-valid pairs; 330/360 gate failures. Blind crops showed standing-subject displacement.                                  |
| `nr-hmd-scene2-20260916T1039Z`                | 36 provenance-valid pairs; 408/432 gate failures. Journal overlap and subject variation.                                             |
| `nr-hmd-fixed-campaign-20260916T1100Z`        | 36 provenance-valid pairs; 211/360 gate failures. Exact fixed matrices and disabled actor AI; face luma decreased up to 13.49 codes. |
| `nr-hmd-outdoor-20260916T111347Z`             | 36 provenance-valid pairs; 420/432 gate failures. AI disabled, physical tracking; no menu events.                                    |
| `nr-hmd-outdoor-fixed-20260916T112605Z`       | 36 provenance-valid pairs; 367/432 gate failures. Journal was initially open and closed during the first sequence.                   |
| `nr-hmd-outdoor-fixed-20260916T112605Z-retry` | All 36 pairs rejected by the too-small frozen position tolerance; image gate not reached. No menu events.                            |

The indoor fixed-pose recording also contains a brief journal open/close
after sequence 2's last acquired frame; no journal event overlaps its
captured frame ranges. Its influence between sequences is not ruled out.
The separate `nr-hmd-fixedpose-20260916T1054Z` prerequisite probe retained
12 pairs. Its inherited crop locations did not match the newly changed
view and were not treated as qualified skin measurements.

The initial direct helper exceeded its output budget after preserving the
full baseline gate. A later helper treated a still-running offline process
as a failure. Saved evidence and process completion were recovered without
replaying those measurements. The rapid capture helper subsequently
awaited offline completion and shared a recorder across the short block.
Deferred exposure retrieval caused the retention gap reported above;
future rapid capture must drain diagnostics after each sequence.

Local feedback receipts:

-   `AUTO-20260916-092605003-8E6F939A`: camera-observer repair/live evidence.
-   `AUTO-20260916-112207107-7244B8B6`: typed Papyrus string-only argument
    schema; boolean strings could return `called: true` without changing AI.
-   `AUTO-20260916-114612629-3C62DB65`: document bounded exposure retention
    when scheduling rapid direct-MCP capture.

## Restoration and limits

Final guarded checks confirmed the original outdoor NR/colour
configuration exactly, NR and character rendering off, both evidence flags
off, TimeScale 20, actor AI enabled, and tracked-input restoration complete.
FOV 0.95 and peripheral TAA off remain as requested. Recordings, screenshot
sequences, pending operations and capture jobs are inactive. Skyrim and
MO2 remain running. Console selection was restored to the outdoor
attempt's original target, Balgruuf; this differs from the empty selection
before the earlier indoor AI-control probe.

The latest evidence contains `cleanup-verdict.json`,
`final-physical-dll-verification.json`, `campaign-index.json`,
`baseline-repeatability-gate.json`, `baseline-summary.json`,
`analysis/private/` and `blind-review/review.json`. All raw evidence
remains local in the task checkout.

This documentation does not change production shaders, renderer ownership,
the image threshold, or the intent of
`5e9cd203876e8e610f756ad1a2f6b98f6f56cdb5`. No comparative NR quality,
neural-detail, candidate-stereo, exposure-recovery, physical-display or
frame-rate flicker verdict is established. The stationary-scene result
should inform assessment-gate review before another request to reposition.
