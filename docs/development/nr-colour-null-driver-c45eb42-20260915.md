# NR colour live retest of c45eb42, 15 September 2026

## Result and identity

Manual-profile transport, Managed inference and Preserve Source processing
ran successfully on the main VR route at both insertion points. Character
tests used the user-requested CSXTest01 save with visible actors.
Exposure capture failed, complete multi-ROI evidence remains blocked, and
transition fallbacks remain unresolved. No production colour domain,
physical headset presentation or clean performance result is established.

-   Compiled source: `c45eb42e16d5f6b8467184bbf91943d94e6796a7`, clean,
    universal Release, branch `work/face-of-gogh-colour-managed-20260914`.
-   Runtime producer Build ID:
    `89d7029adfdd8677df543e1fcae7c1cced3cd7d49eb2b49417b134cfb6ae21af`.
-   Prior build receipt: DLL size 24,626,688 bytes, SHA-256
    `c456fa714e72d4317e10bafd3451736c8d2a02c4e1aa9f46f9f36f5ca0fbfcd3`.
    This is retained build evidence, not a fresh physical deployment check.
    The user explicitly stopped further AIO validation.
-   Runtime log confirms `NeuralColor.ini 1-2-0 successfully loaded`.
    Producer shader-cache ABI:
    `f4d79afa3f0923126f0fe069ef8898a09448064bdd61466f63b240cb5138853f`;
    shader compiler `d3dcompiler_47.dll:10.0.26100.9444`.
-   Direct DevBench MCP transport, SkyrimVR.exe PID 8296, port 8921.
    Screenshot service session:
    `d2720b9f-6ed0-a27f-efdf-d28b4a6abcd4`.
-   Running DevBench reports `1.18.1+pt.1.16.0.pr6.95aeb04`.
    It does not report the packaged recording correction,
    `1.18.1+pt.1.16.1.nr-recording-idle`, source
    `afa380a7a2ef411fc4c9c44df38d4a00ed7397f5`.
-   NR runtime 310.8.0, initialized, identity `patched-recognized`, SHA-256
    `8270B350CD82DE5CE89806872CDD6B6A9249B80836B91BBEB3573470744CC206`.
    Driver parameter-core SHA-256:
    `357E3AA2F0CD5F1ED8DE6BA96159F55BAB0D13A336F97EC6D59F3B620B901C14`.
-   Windows hardware inventory reports NVIDIA GeForce RTX 5070 Ti Laptop
    GPU, driver 32.0.16.1088, and AMD Radeon 890M integrated graphics.
    Inventory alone is not proof of the active D3D adapter.
-   Valve null HMD supplied a valid connected standing pose at 1.7299999 m;
    tracked controllers and managed pose replay were unavailable.
    The user explicitly authorized feasible null-driver testing.
-   DLSS, FOV-only scale 0.95, 1512x1680 per eye; main route only.
    No external integration change forced submit-route coverage.

The retained MO2 workspace is
`C:/src/skyrim-vr-upscaler-standalone/build/mo2-automation/sessions/workspaces/20260905t210822z-face-of-gogh-ghidra-save-testbas-a09ca515.json`,
profile
`Codex Task - 20260905t210822z-face-of-gogh-ghidra-save-testbas-a09ca515`.
This run attached to the user's existing game; it acquired no MO2 launch
lease. No package replacement or AIO revalidation ran.

The preceding build passed all 14 standalone NR tests, including colour,
exposure, character-mask and framebuffer WARP tests. Its final-LDR blend
WARP check covered 104 cases; the companion DevBench host suite passed 76
cases. These prior results are in
`build/validation/nr-live-20260915/verified-aio-receipt.json` and the build
logs. They were not rerun during this live campaign. The old build's
failures remain in
[nr-colour-null-driver-retest-20260915.md](nr-colour-null-driver-retest-20260915.md).

## Evidence and method

Raw evidence is local at
`C:/src/skyrim-community-shaders/build/validation/nr-live-20260915/retest-c45eb42-20260915033300Z`.
Numbered request/response receipts preserve the live mutations and reads.
`assessment-index.json` indexes candidates and all capture artifacts;
`live-evidence-audit.json` retains hashes, full counter snapshots,
recording metadata and warning/error log lines.

Accepted colour assessments require three distinct fresh coherent groups
after the configuration frame floor and 16 warm-up frames, matching
revision, insertion, generation, source frame and producer. The repository's
`fresh_groups` and `assess_samples` functions were used offline; Preserve
Source has a separate finite-pixel and endpoint assessment. Configuration
acceptance is never treated as a rendered pass.

There are 28 saved assessment files: 25 pass their stated private-colour
checks and three are incomplete. One incomplete late-proxy sample set
passed after a fourth observational read at the same revision; its original
two-group result remains preserved. The other two incomplete sets are
multi-ROI. These counts include the extension, not 28 independent profiles.

All 24 still-capture requests completed: 72 PNGs with committed artifact
receipts, matching byte sizes, SHA-256, dimensions, PNG chunk CRCs and
decompressed sizes. Each request contains left/right 1512x1680 images and
a 3024x1680 side-by-side image from one submitted-eye acquisition cycle.
No physical display or compositor timing correctness is inferred from
these artifacts.

## Coverage

| Check                                                    | Result              | Evidence and limit                                                                                                                                                                                                                                        |
| -------------------------------------------------------- | ------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Centre identity transport                                | PASS                | Three groups, zero measured round-trip error.                                                                                                                                                                                                             |
| Centre linear/sRGB and reversible-proxy transport        | PASS                | Three groups each, zero measured round-trip error.                                                                                                                                                                                                        |
| Centre Managed inference, all three manual candidates    | PASS                | Three finite coherent groups each, real Feature 18 evaluations and complete outer eye masks.                                                                                                                                                              |
| Final-LDR transport, all three manual candidates         | PASS                | Three groups each, zero measured round-trip error.                                                                                                                                                                                                        |
| Final-LDR Managed inference, all three manual candidates | PASS                | Three groups each after extending the proxy case. No recurrence of the old missing-framebuffer-UAV contract failure. A separate character transition failure is retained below.                                                                           |
| Raw NR, centre and Final LDR                             | PASS                | Actual inference and submitted-eye captures; this is a functioning baseline, not a verified model input domain.                                                                                                                                           |
| A/B hidden                                               | PASS                | Zero baseline difference, inference continues, input epochs remain [15,13] across the display-only switch.                                                                                                                                                |
| Preserve Source                                          | PASS                | Seven sets: zero detail, detail 0.5, appearance 0/0.5/1, character selection, all-zero bounds, and detail 2 / appearance 1 / maximum stops 2. All have three finite groups. Both zero-detail/zero-appearance sets have zero measured baseline difference. |
| Character single enclosure, centre                       | PASS                | Visible CSXTest01 actors; frames 203755, 204154, 204575, both outer eyes committed.                                                                                                                                                                       |
| Character Managed, Final LDR                             | PASS after recovery | Frames 233030, 233302, 233583; final recovery frames 261021, 261322, 261631. Initial transition failure is not erased.                                                                                                                                    |
| Empty category selection and force-zero mask             | PASS                | Both eyes bypass Feature 18; no inference claim for the bypass.                                                                                                                                                                                           |
| Experimental multi-ROI                                   | BLOCKED             | Secondary slots 4 and 5 really evaluated, but the two bounded sample sets do not provide three complete coherent groups.                                                                                                                                  |
| Submit route                                             | NOT TESTED          | Not active in this setup; stale submit telemetry was excluded.                                                                                                                                                                                            |
| Captured-exposure candidates                             | BLOCKED             | No accepted engine exposure samples at either insertion point.                                                                                                                                                                                            |
| Performance comparison                                   | BLOCKED             | Exact performance-neutral probe unavailable. No profiler campaign was started.                                                                                                                                                                            |
| Physical headset, flat Skyrim and bright exterior        | NOT TESTED          | Outside this attached null-HMD interior pass.                                                                                                                                                                                                             |

Transport-bypass commits are distinct from inference: the bypass can
produce complete output eye masks with attempted mask zero. Real Feature
18 evaluation counters did not increase during that diagnostic copy.
Appearance-one equivalence to Managed was not inferred from different
frames. Source-relative RGB differences do not rank colour correctness.

The automated control sweep exercised NR on/off, presets 0–4, styles 0–3,
intensity/tone/structure/skin endpoints 0 and 2, all four staged/direct and
per-eye/batched implementations, individual optimization controls, and
subrect scales 0.25/0.6/1. Explicit style/slider edits select custom preset
zero; separate preset-only calls confirmed intended preset selection.

Character controls covered face/skin/hair selection and strengths 0/1,
distance 0/30 m, minimum face size 1/4096 pixels, margin 0/1/0.25,
hold 0/30/3 frames, feather radius 0/4/1, depth threshold 0/0.05/0.002,
adaptive/depth/visibility/isolation flags, all four debug views and all
six mask-test modes. The standard character centre path correctly forces
staged output despite a requested direct implementation.

## Characters, menus and temporal checks

CSXTest01 loaded successfully after the user's request; its visible
characters replaced an earlier Farengar view occluded by a map board.
That earlier empty result is not counted as positive character inference.
The Test01 game clock was held at 10.0050563812 hours with timescale zero.
Actor animation continued. Earlier standard-profile captures span changing
game time and cannot be treated as pixel-matched quality comparisons.

Bounded native camera steps used X offsets -8, 0, +8, 0 game units and yaw
offsets -0.04, 0, +0.04, 0 radians around the same anchor. Both eyes committed
through frames 250806, 251374, 252073 and 252599, with no new NR evaluation
failure. This is discrete native camera movement, not smooth tracked-head
replay. Camera ownership was released.

Inventory opened and closed; both eye images retained readable UI and
visible characters. Inventory did not pause world rendering. Journal
opened and reported gamePaused=true at frame 253865 while the main route
still had fresh world-frame data and complete NR eye masks. Journal then
closed to HUD only. This does not exercise retained-world submit continuity.

Mode, insertion, exposure-observation, enable/disable and A/B transitions
were exercised. Changed input interpretation advanced input epochs;
display-only A/B did not. Final colour samples were fresh after these
changes. Controlled actor crossings, matched standard/character rectangles,
smooth head motion and a controlled lighting/exposure transition remain
NOT TESTED.

## Unresolved failures and implementation follow-up

1. **FAIL: exposure observation.** Two producers registered, zero accepted
   captures, 18,430 rejected observations. The last rejected binding is
   frame 119562, with no recorded PS or source identity. Reason:
   `HDR draw boundary has no live PS/AvgTex t2; no exposure assumed`.
   Both insertion points were tested; no ratio or gamma was measured.
   `hooksInstalled=0` reflects the draw-observer implementation, while
   advancing rejection frames demonstrate that it ran. The early return
   currently conflates absent PS and absent t2. Establish the actual binding
   boundary and retain each missing component independently before changing
   the hook. Do not guess spatial exposure or borrow another frame.

2. **FAIL: transition fallbacks.** The log contains two
   `eErrorDuplicatedConstants` events at frames 107519 and 167551 for
   FoveatedCenter viewport 4352, followed by normal full-frame DLSS fallback.
   Twelve foveated fallback log lines occur across the campaign, including
   character-policy changes; not all have that duplicate-constants reason.
   One explicitly reports lost character visual-isolation resources.
   The normal-DLSS duplicate policy must not be widened without comparing
   the complete constants and ownership contract.

3. **FAIL: character/late preparation transition.** Final-LDR resources
   failed for the left eye at frame/source frame 231893 while entering the
   character late path. Target and UAV formats were 28, both targets were
   3024x1680, typed-UAV HRESULT was success, and normal DLSS was preserved.
   Character preparation failures first reach one in the following
   `0320-late-character-managed-outer.response.json`.
   Later real inference and final recovery passed. The broad resources
   stage does not identify the exact failing preparation operation, so this
   is not proof of a framebuffer-UAV regression or a diagnosed root cause.

4. **BLOCKED: complete multi-ROI colour evidence.** Full-body revision 25
   yields zero complete groups; face-only revision 26 yields two coherent
   groups at frames 215453 and 215814 with slots {0,1,4}. Later primary and
   secondary slots have different frame keys as the layout changes.
   Real slot 5 success is preserved. API v2 lacks the immutable per-pair
   expected-region manifest needed to disambiguate changing participation.
   Excluding stale secondary slots to manufacture a pass is not valid.

5. **FAIL: cold recording status on the running DevBench.** The old runtime
   returns JSON type_error.306 before recording starts. Start, running
   status, stop and subsequent idle status worked. The packaged correction
   is not live-verified because the running version is older. Feedback
   `AUTO-20260915-012012888-2B4B20E3` was amended. Deployment was not
   reinspected or replaced after the user stopped AIO validation.

Final observed NR counters at frame 265313: 344,822 Feature 18 evaluations,
421,102 output commits, 201,479 successful stereo attempts, zero NR
evaluation/validation/stereo failures, zero removals, quarantines or failed
resets; ten resets succeeded. There were 16 resource rebuilds and seven
runtime/interop initializations across the deliberate control changes.
Character preparation failure count is one. These narrower zero counters
do not negate the logged transition failures above.

Before disabling optional diagnostics, colour counters reported 383,804
prepared/reconstructed, zero failed, 76,280 bypassed, 383,206 measurements
and 568 dropped asynchronous measurements. Dropped readbacks remain
visible; incomplete groups were never accepted.

Other startup warnings, including the VR scene-graph culling guard's
unexpected-instruction rejection and settings-file write denial, remain
in the audit. They were not attributed to NR.

## Performance and cleanup

The exact standalone neutral-state probe was absent from the discovered
live API. The protocol requires performanceDistorted=false,
physicalStateKnown=true and unchanged ownership epoch before and after
measurements. Thus no controlled NR off/raw/Managed/Preserve Source
comparison ran. Retained Feature 18 GPU timings and CPU enqueue timings
are diagnostic only and are not combined into a benchmark.

Five activity parts preserve 57,799 tracking samples and 1,309 activity
events. Part 2 hit the 1,800,000 ms sampling limit; its last tracking sample
is at 1,799,978 ms, although stop reports 2,180,400 ms. The 380,422 ms tail
has no tracking coverage. Rotated parts and their intervals must not be
presented as one continuous recording. All other parts stopped below the
limit; all five exact files and hashes are retained.

Final cleanup confirmed recording=false/state=idle, zero active screenshot
sequences, pending operations, outstanding jobs or failed screenshot
artifacts, and freeCam=false/freeCamOwned=false. Optional colour
diagnostics and observation-only exposure capture are disabled.
NR, FOV and character isolation remain enabled, Managed identity/manual
profile, Final LDR, preset/style 3, experimental multi-ROI off.
No experimental profile was saved as a production default and no settings
restore was performed. The attached game remained loaded with HUD only.

## Evidence verification

Executed from the repository root, with the evidence directory above as
the script argument where required:

-   `python <evidence>/validate_samples.py <case>.samples.json`: recorded
    per-case results, including rejected incomplete groups.
-   `python <evidence>/validate_preserve.py <evidence>`: seven passing sets.
-   `python <evidence>/index_evidence.py <evidence>`: 28 assessments,
    25 passing files, 24 completed stereo requests.
-   `python <evidence>/audit_current.py`: 72 verified PNGs, five recordings,
    96 NR snapshots and 155 colour snapshots. This audits live evidence only;
    it performs no AIO/deployment validation.

Representative stereo images:

-   [Character centre](C:/src/skyrim-community-shaders/build/validation/nr-live-20260915/retest-c45eb42-20260915033300Z/captures/test01-single-managed_stereo.png)
-   [Character Preserve Source](C:/src/skyrim-community-shaders/build/validation/nr-live-20260915/retest-c45eb42-20260915033300Z/captures/preserve-character_stereo.png)
-   [Character Final LDR](C:/src/skyrim-community-shaders/build/validation/nr-live-20260915/retest-c45eb42-20260915033300Z/captures/late-character-managed_stereo.png)
-   [Inventory overlay](C:/src/skyrim-community-shaders/build/validation/nr-live-20260915/retest-c45eb42-20260915033300Z/captures/late-inventory_stereo.png)
-   [Final recovery](C:/src/skyrim-community-shaders/build/validation/nr-live-20260915/retest-c45eb42-20260915033300Z/captures/character-final-recovery_stereo.png)

The final left/right images show the same character scene with expected
stereo viewpoint differences and no obvious gross corruption. That visual
observation does not establish the correct NR colour treatment.
