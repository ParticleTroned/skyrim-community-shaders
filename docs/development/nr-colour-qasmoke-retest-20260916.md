# QASmoke HMD baseline follow-up

The QASmoke retry admitted all 36 native stereo pairs against the frozen
camera and provenance checks. Its unchanged NR-off references still failed
255 of 288 regional gradient comparisons. Mean face brightness varied by
less than one encoded code value, while individual pixels differed more.
The gradient result alone does not establish movement or an NR defect.
Independent blinded review did identify a blink and a contaminated wall
crop. No comparative NR candidates were dispatched or correction inferred.

## Runtime and evidence

The user's renewed request authorized the replacement Skyrim VR process,
PID 27560, started at 2026-09-16T12:03:31.5136862Z. It was in QASmoke
(Editor Smoke Test Cell, form 0x00032AE7), with physical tracking and actor
AI left unchanged. TimeScale was temporarily set to zero; captured game
hour was 18.895204544067383. The requested FOV 0.95, enabled foveated
dispatch and disabled peripheral TAA were applied and verified.

Compiled source remained `e3e731a83ff72387bdeebdae5b038cf87d826656`.
Build ID:
`4385d3607500f6dd1006d3c90bd9a15b89eb193ab3795b4300b9155ec37c38e9`.
The mapped physical CommunityShaders.dll was 24,922,112 bytes, SHA-256
`ae697555b7cb4e541dbb89428da89decb41c64f7aadf389e2d6105243130dfcb`.
The DevBench host was separately mapped and matched. Final checks retained
the same process, one enabled loose CSX provider, no Overwrite/unmanaged
DLL, and an exact adjacent build-manifest/AIO-receipt match with the
DevBench bridge enabled.

Evidence directories under `build/validation/`:

-   `nr-hmd-new-scene-20260916T121535Z`: initial camera-rejected block.
-   `nr-hmd-new-scene-20260916T121535Z-fresh-reference`: new frozen plan,
    fresh camera reference, accepted capture block and offline assessment.

Each attempt retains its immutable plan, raw manifests, original
2468 by 2740 PNGs per eye, correlated recorder, diagnostics and explicit
not-run schedule entries. Only direct typed DevBench MCP controlled the
game. Canonical `hmd_capture.py` and `hmd_assess.py` provided offline
validation and analysis. No performance campaign was run.

## Camera reference retry

All 36 pairs in the initial block failed the predeclared view-matrix
tolerance of 0.001. Drift from the earlier scout was 0.002403915 to
0.002781749; drift within the captured block was only 0.000442021.
Projection was unchanged and position-adjust drift remained below 0.2
engine units. The rejected captures remain rejected.

A separate plan acquired a fresh camera reference immediately before
dispatch, with the same view, projection and position tolerances and the
same regions. Its plan SHA-256 is
`b2e62a47be3e741e388dde5b2ea40a01ce4d59f5dddff28611f9bf431f9eeff3`.
All three 12-pair sequences then passed. Maximum drift was 0.000533208
for view matrices, zero for projection, and 0.07861328125 engine units for
position-adjust vectors. No tracking override or actor-AI control was used.

The successful block ran from 12:32:21.887Z to 12:33:03.440Z. Its shared
67,690 ms recording had no menu/activity events, duration limit or
unrecorded tail. Observed within-sequence spacing was 545.042 to
617.274 ms; this cannot qualify HMD frame-rate flicker.

## Image measurements

Fixed native crops were selected independently for each eye before
capture. The image threshold remained 0.15. No image registration,
normalization, white balancing or threshold relaxation was applied.

| Region      | Eye             | Median gradient mismatch | Minimum mean-luma delta | Maximum mean-luma delta |
| ----------- | --------------- | -----------------------: | ----------------------: | ----------------------: |
| Face        | Left            |                   0.7306 |                 -0.2952 |                 +0.2419 |
| Face        | Right           |                   0.7289 |                 -0.4221 |                 +0.9672 |
| Armour      | Left            |                   0.7161 |                 -0.4968 |                 +0.0109 |
| Armour      | Right           |                   0.7199 |                 -0.4503 |                 +0.0055 |
| Wall shadow | Left            |                   0.2789 |                 -0.0196 |                 +0.0448 |
| Wall shadow | Right           |                   0.2706 |                 -0.0205 |                 +0.0532 |
| Stone wall  | Left            |                   0.1976 |                 -0.0634 |                 +0.0488 |
| Stone wall  | Right, excluded |                   0.1885 |                 -0.0698 |                 +0.0499 |

Luma deltas are encoded 0–255 code values against each eye's own first
reference. The right stone-wall values retain the original computation;
blinded inspection subsequently excluded that crop from qualified material
assessment because it includes the black submission border. No crop or
gate result was retroactively changed. Median per-pixel RGB absolute error
was 8.5470 left and 7.8414
right in the face crops. A small regional mean change therefore does not
imply identical pixels. Gradient disagreement alone does not isolate
movement, resampling, fine noise, texture changes or illumination.

The pre-dispatch gate rejected 255 of 288 comparisons. Canonical offline
analysis accepted three sequences, retained the other 66 schedule steps
as not run, and flagged 191 regional rows using its different reference.
These counts measure different comparisons and are not interchangeable.

All 36 engine-exposure companions were retrieved after their respective
sequences and remained available. Available measurements reported ratio
one and frame gamma exponent one. The 72 unavailable per-eye inference
observations are expected with NR off. Engine exposure evidence does not
establish the model's input domain.

No strong highlight/clipping region was available in this scene. Neural
detail, candidate colour fidelity, candidate stereo consistency and
exposure recovery remain untested.

## Blinded review

A fresh reviewer inspected all 72 native fixed crops and 18 overviews:
both eyes, all four regions, and ordinals 1, 6 and 12 in each sequence.
Its structured assessment was saved before revealing any settings mapping.
All 18 attempted full-resolution original loads failed in the image
viewer; native detail coverage outside the crops is not claimed. The
review received approximate cadence, not exact per-image timestamps, and
did not inspect unsampled frames or continuous playback.

The reviewer directly observed closed eyelids in both eyes at sequence 1,
ordinal 6, with open eyes in the surrounding sampled frames. That facial
subregion is confounded by a visible blink. Gross body pose, framing,
armour contours and broad illumination otherwise appeared stable. Fine
grain was present, but its temporal change and cause remained uncertain.
The blink does not explain all regional gradient failures.

Every right-eye stone-wall crop contains a black border strip, approximately
one fifth of its width. That strip is not material evidence. The two wall
crops also cover different stone texels, so direct binocular texture
matching is unsupported. Own-eye repeat measurements remain preserved;
the whole right-wall crop is excluded from a qualified material verdict.

All four candidate objectives were indeterminate because no candidates
were supplied. Sparse source repeats do not establish useful neural
detail, candidate colour fidelity, candidate temporal stability or
candidate stereo consistency.

## Earlier lower-sunlight follow-up

Before the process replacement, the user requested less-sunlight testing.
Evidence remains in `nr-hmd-lower-sun-20260916T115411Z`. That independent
plan admitted 36 native stereo pairs with physical tracking and unchanged
actor AI, no menu events and all 36 engine-exposure companions available.
Its fixed gate rejected 420 of 432 comparisons. Maximum face mean-luma
increases were only 0.7181 left and 0.7920 right; face gradient mismatch
medians were 0.8199 and 0.8222. Sparse spacing was 482.445 to 514.188 ms.

The user stopped the subsequent blinded review, so no completed blind
assessment is claimed for that attempt. NR/colour restoration was
acknowledged exactly and TimeScale 20 was queued. The game process exited
during final verification; applied TimeScale readback and the final old
dispatcher check were not established. The replacement process was not
modified until the user's new request. Its initial TimeScale was 20.

## Restoration and workflow findings

Final QASmoke readback matched the complete pre-capture NR/colour
configuration, with the requested FOV 0.95 and peripheral TAA off retained.
NR and character rendering were off, both evidence flags were off, and
TimeScale was 20. Recording was inactive; screenshot active sequences,
pending operations, queued captures and outstanding worker jobs were zero.
The game remained running. No actor AI or tracked input was changed.

Local feedback `AUTO-20260916-123837695-E7581367` records the practical
need to complete reusable preparation before acquiring the camera
reference. The separate retry demonstrates that ordering improvement
without changing tolerances or reclassifying rejected captures. Retrieval
after each sequence also retained every engine-exposure companion in the
lower-sunlight and QASmoke blocks; this is scheduling evidence, not a
toolkit source fix.

This follow-up changes documentation only. It does not change production
shaders, renderer ownership, the image gate, or the intent of
`5e9cd203876e8e610f756ad1a2f6b98f6f56cdb5`. The earlier
[camera repair and outdoor evidence](nr-colour-hmd-retest-20260916.md)
remain separately preserved.
