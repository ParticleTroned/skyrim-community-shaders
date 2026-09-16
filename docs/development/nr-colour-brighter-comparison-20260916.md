# Brighter-scene NR colour comparison

The brighter QASmoke scene supports a useful, scoped NR colour comparison.
Raw NR and Managed identity consistently increased face brightness versus
their inference-running, display-hidden controls in both eyes and both
orders. Preserve Source kept face tone close to those controls. This is a
character-mask result for the captured scene and settings, not a model
input-domain proof or a universal production-default recommendation.

## Findings from matched controls

The table reports shown minus display-hidden mean encoded luma, in 0–255
code values. Each entry averages one 12-pair sequence against its matched
control. Both sets keep inference running; only the model edit's display
state changes. Forward shows the edit first; reverse shows the control
first.

| Mode             | Left, forward | Left, reverse | Right, forward | Right, reverse |
| ---------------- | ------------: | ------------: | -------------: | -------------: |
| Raw NR           |        +4.311 |        +4.173 |         +6.037 |         +6.633 |
| Managed identity |        +4.361 |        +4.455 |         +6.047 |         +6.933 |
| Preserve Source  |        -0.161 |        -0.347 |         -0.248 |         -0.297 |

Raw and Managed identity behaved similarly. All RGB channels increased;
the relative channel changes are consistent with the blinded observation
of paler, less saturated skin. The face-brightness effect remains positive
after reversing the order, despite gradual dimming across the overall
run. Armour, shadowed ground and ordinary ground comparisons stayed
within 0.109 mean-luma codes across every shown/hidden pair.

These are exploratory effects, not independent-frame significance tests.
The five NR-off sequence means spanned 4.243 face-luma codes in the left
eye and 3.017 in the right. Thus the right-eye Raw/Managed effect exceeds
that full-run baseline span, while the left-eye effect is around its
size. The repeated direction, nearby hidden controls, unaffected material
and background controls, and original-pixel review support the localized
colour finding more strongly than a single RGB-error score would.

The small forehead crop is not an independent quantitative confirmation:
its NR-off means drifted by 28.592 codes left and 16.862 right, larger than
its shown/hidden effects. It remains useful for scoped visual inspection,
with sampling/pose/expression limitations. It was not moved, normalized
or replaced after candidate inspection.

## Blinded image findings

Two fresh review contexts assessed three anonymous mode/control comparisons
without settings mappings or technical metrics. One reviewer assessed
Preserve Source; the other assessed Managed identity and Raw separately.
Initial findings were saved before reversed-order review. Combined
scorecards were saved before revealing the settings mapping.

| Mode             | Colour in inspected crops              | Retained facial detail              | Useful new neural detail |
| ---------------- | -------------------------------------- | ----------------------------------- | ------------------------ |
| Raw NR           | Hidden source preferred in both orders | Hidden source preferred             | Not established          |
| Managed identity | Hidden source preferred in both orders | Hidden source preferred             | Not established          |
| Preserve Source  | Tie within visible source variation    | Tie within visible source variation | Not established          |

Raw and Managed identity appeared consistently lighter and less
yellow-brown, with pale grey-pink mottling replacing finer coherent skin
texture. The source control preserved warmer skin and finer facial detail.
The overall regional preference had medium confidence and survived
reversing the acquisition order. This is a visible retention observation;
it does not call the source a neural winner simply because its RGB error
is zero.

Preserve Source maintained comparable source colour and retained detail
in both orders. No repeatable coherent new-detail advantage was visible.
Armour, shadow and ground crops tied within baseline variation for all
three comparisons. No consistent broad one-eye tint or gross structural
divergence was identified after own-eye reference comparison; fine or
full-field binocular consistency remains unestablished.

Each comparison covered 108 native face/forehead crops at ordinals
1, 6 and 12, plus 54 middle-frame material/shadow/background crops,
including explicitly reused matching source images. Overviews provided
scene context. Full original-resolution image viewing failed; valid
original hashes and Pillow decoding do not substitute for successful
visual inspection outside the native crops. Temporal preference,
frame-rate flicker, exposure recovery and physical-display behaviour are
not established by these sparse samples.

Source expression/pose, sword angle and foreground hand changes were
preserved as confounds. Reviewers excluded the affected occlusions and
did not treat fine pixel disagreement as proof of movement. The practical
result is a preference for Preserve Source's colour retention in this
scene, with no demonstrated additional neural-detail benefit.

## Authorized scope

The user explicitly requested a useful comparison without stopping solely
because of a protocol threshold. The new immutable plan retained the
canonical 0.15 gradient threshold and camera tolerances as diagnostics.
A separate frozen scope selected 17 of the canonical 69 schedule steps:

`1, 2, 3, 5, 6, 8, 9, 11, 12, 14, 16, 17, 19, 20, 22, 23, 25`.

This includes three unchanged initial baselines, Raw NR, Managed identity
and Preserve Source with their inference-running/display-hidden controls
in both forward and reverse orders, plus two flanking NR-off baselines.
The other 52 steps are explicitly not run by scope; full three-repeat
protocol qualification is not claimed. No conversion profile was invented
from appearance.

The final plan SHA-256 is
`b4c36bb99f9b82fed1cbe76db7ab937a7c002aaf4fccd144a42acd103a3b63d1`.
The selected canonical schedule and settings mapping were frozen before
capture. Source images were compared at fixed native crops separately
for each eye, without alignment, histogram matching, white balancing or
normalization. Face and forehead, armour, ground shadow and background
regions were declared before candidate inspection. A strong isolated
highlight/clipping region was unavailable.

An earlier 12-pair block was interrupted when the user reported scene
movement and requested a restart. Its recording, plan and captures remain
separate and excluded from this comparison.

## Capture and attribution

All 17 selected sequences completed, yielding 204 native stereo pairs:

-   60 pairs with NR off, across five baselines.
-   72 pairs with the model edit shown, across six candidate sequences.
-   72 pairs with inference running and the model edit hidden.

Every retained pair passed the canonical artifact/hash, dimensions,
source, eye identity, current configuration, revision/input epoch,
frame-age and NR-outcome checks. Both eyes showed the required successful
inference in enabled conditions. Output-commit state matched shown versus
hidden, with no attribution errors. Original images are 2468 by 2740
per eye, `hmd_submission`, `fallback=reject`, PNG `sdr_srgb`.

The script used canonical `hmd_assess.py` pair checks, anonymous-image
preparation and regional/temporal/stereo measurements, and
`hmd_capture.py` baseline and camera diagnostics. A task-local adapter
retained every attributed frame for the explicitly authorized exploratory
sequence means. It kept the canonical strict-filtered results separately.
The canonical strict protocol is not relabeled as passed.

The initial image gate flagged 346 of 360 comparisons. Across the full
run, 1,920 of 2,040 regional rows were gradient-flagged, and all 204 pairs
exceeded the scout-relative view tolerance. Maximum view-matrix difference
was 0.0154151064, projection difference zero, and position-adjust difference
0.1806640625 engine units. These findings remain in the evidence.

Matched shown/hidden sequence-mean camera differences were much smaller:
view components ranged from 0.0002418831 to 0.0007202419; position-adjust
components ranged from 0.00382487 to 0.01806641 engine units. These are
descriptive differences between sequence means, not a replacement camera
gate or proof of exact pixel alignment. Visible expression, sword and hand
variation is assessed separately.

The shared recording retained 813,206 ms with no duration limit or
unrecorded tail. Cell, interior/weather identity and frozen game hour
matched. Two journal open/close intervals occurred before/between early
baseline sequences. Event-frame audit found no acquired pair with the
journal open. Inter-sequence effects are not ruled out.

Actual within-sequence spacing was 639.108–871.985 ms, despite the requested
500 ms cadence. The short sparse sequences cannot establish headset
frame-rate flicker or full temporal stability.

All 204 engine-exposure companions remained available through retrieval
after each sequence. The 408 per-eye inference-exposure stamps were
unavailable. Engine exposure observations are distinct from inference
bindings and do not establish the model's input domain.

## Runtime identity and restoration

The scene was QASmoke in Skyrim VR PID 27560, started at
2026-09-16T12:03:31.5136862Z. The user's requested foveated dispatch,
FOV 0.95 and peripheral TAA off were retained, with existing DLSS and
character face/skin isolation settings fixed. Actor AI and physical
tracking were unchanged. TimeScale was temporarily zero.

The insertion was Upscaled Center, with NR preset/style 3, intensity 0.8,
local tone 0.75 and local/skin structure 0.9. Face and skin strengths were
one, hair disabled, and character visual isolation enabled. Colour tests
used detail strength one, appearance mix zero and maximum detail stops one,
with an identity transform and manual exposure multiplier one. The input
domain remained explicitly unknown.

Compiled source:
`e3e731a83ff72387bdeebdae5b038cf87d826656`.
Build ID:
`4385d3607500f6dd1006d3c90bd9a15b89eb193ab3795b4300b9155ec37c38e9`.
CommunityShaders.dll: 24,922,112 bytes, SHA-256
`ae697555b7cb4e541dbb89428da89decb41c64f7aadf389e2d6105243130dfcb`.

The preserved same-process mapping identifies the physical CSX AIO and
DevBench modules. Final read-only verification matched the physical CSX
DLL to the adjacent manifest and AIO build receipt, including the compiled
source, Build ID and enabled DevBench bridge. There was one enabled loose
CSX provider and no DLL in Overwrite or unmanaged Data.

Final guarded readback matched the full pre-capture NR/colour settings:
NR and character rendering off, evidence flags off, TimeScale 20, and the
requested FOV/TAA settings retained. Recording, screenshot sequences,
pending operations and outstanding capture jobs were inactive. The same
game process remained running.

## Evidence and validation

Evidence is local under:

`build/validation/nr-hmd-brighter-20260916T125450Z-restart`.

The interrupted attempt remains under the same name without `-restart`.
Retained artifacts include the frozen plan/scope, 204 stereo pairs and
committed manifests, complete recording, per-sequence exposure sidecars,
guarded mutation/restoration receipts, strict diagnostics, exploratory
regional CSV/JSON, all-frame baseline envelopes, shown/hidden contrasts,
confound audit, anonymous review images and blinded scorecards.

All 17 selected sequences had 12 attributed pairs and zero attribution
errors. Scoped documentation formatting and staged whitespace checks
passed. No production rebuild or performance test was needed for this
documentation-only update.

Feedback is noted here: lossless native-pixel tiles with source hashes,
coordinates and coverage accounting could avoid repeatedly failing the
full-original image viewer while preserving native detail. Automatic
approval review rejected an optional local feedback-queue submission
because its metadata payload and destination lacked explicit authorization.
No queue write or indirect workaround was performed; the note remains in
this task report.

This report changes documentation only. It does not alter renderer
ownership, production shaders, defaults, or the intent of
`5e9cd203876e8e610f756ad1a2f6b98f6f56cdb5`. It follows the earlier
[QASmoke baseline report](nr-colour-qasmoke-retest-20260916.md).
