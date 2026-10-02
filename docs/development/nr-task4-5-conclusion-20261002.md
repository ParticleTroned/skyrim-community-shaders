# Tasks 4 and 5 conclusion — 2026-10-02

## Decision

**Task 4 is concluded for the implemented culling/empty-proof scope.** The
new installed DLL passed the remaining VR empty-guide, asymmetric-eye and
accepted B-to-C checks. Production fixtures cover conditions that could not
be induced live. The original watchdog's exact failing GPU dispatch remains
unknown; two successive DLL sessions passed after the DevBench ownership
correction. This is bounded regression evidence, not proof of the original
incident's complete causal chain.

**Task 5's implementation and qualification are concluded with automatic
sparse-dispatch adoption rejected.** Current-ready bounds narrow mask work,
compatible typed-depth copies retain exact results, and the GPU support
candidate has reference-equivalence and live cost evidence. Its heavy-feather
case is useful, but neither the no-feather live case nor dense synthetic
cases meet improvement-or-neutral. The candidate therefore remains
session-only, DevBench-only and default off. This closes the evaluation
without claiming a general production speedup or enabling a regression.
Production promotion would require a separately qualified crossover policy.

Task 2 stays concluded. Task 6 is the next planned implementation task.
There is no new native context, output-ownership, allocation, history,
equipment-category or NGX transport policy in this follow-up.

## Exact live identity and evidence

-   Implementation: `4bd08e6f5a07c7efb28cabf078ce67f67532fde3`.
-   Producer Build ID:
    `7aaec1186f239dbe2917c2f09adba1327a3886ec7defeef53d31f1140ceee0e0`.
-   Compiled base: `5ed4e57dd998664392b844bac24f581b622132b9`; dirty digest:
    `895aa65a125700cc7b94e981630778251a25e98d8a4cf8e4c9d3a069913099a6`.
-   DLL: 31,229,952 bytes; SHA-256:
    `5f1d7032a7fda5313022294411c73bf635f32ac215e69f973a1aeb027bde645a`.
-   Sole enabled loose provider:
    `CSX_AIO-main-vr-nr-NR-Task4-5-DevBench-20261002-7aaec1186f23`.
    Physical DLL, adjacent manifest, archive receipt and runtime producer
    matched. Overwrite and unmanaged Data had no replacement DLL.
-   Skyrim PID 44192, started `2026-10-02T17:53:12.4428949Z`, Bannered Mare,
    existing null HMD. Main C input was 1008×1120 per eye, output 1512×1680.
-   Local evidence: `build/validation/nr-task4-5-followup-20261002/`.
    `qualification-audit.json` records **24 completed sequences, 384 exact
    frames and 384 matching image hashes**, plus **14 completed 300-frame
    profiler windows (4,200 frames)**. `live-audit.json` preserves per-frame
    source/capture identity, calls, area, proof, dispatcher and native timing.
    No finalized execution was failed, missing or replaced by a later plan.

Controls used the bundled DevBench transport with pinned build/process
identity and performance-neutral admission. Renderer-busy refusals were
preserved as refusals, and Console-assisted admission was used for NR/FOV
changes. There was no blind retry after an unknown mutation outcome.

## Task 4 acceptance

| Case                                 | Evidence and result                                                                                                                |
| ------------------------------------ | ---------------------------------------------------------------------------------------------------------------------------------- |
| Cold A with current CPU empty proof  | Runtime/interop initialization, resource creation and native evaluation initially remained zero                                    |
| A empty, 300 profiler frames         | No full-resolution guide, NR depth-guide, character mask or character-composite work; category capture and bounds discovery remain |
| A entry                              | Both eyes resumed; no empty/re-entry resource rebuild                                                                              |
| A→B, B→C, repeated B→C               | Accepted transitions, successful backend resets and sustained C work; no watchdog or NR fault                                      |
| C distance exclusion and return      | Native evaluations stayed 37,866 across frames 59,282–59,536, then resumed without resource recreation                             |
| C offscreen/disappearance and return | Both empty sequences had zero calls/pixels and current CPU proof; return resumed work                                              |
| Asymmetric A                         | All 16 exact frames had one empty eye and one evaluation of 317,312 pixels; ordinary other-eye delivery remained coherent          |
| Intentionally full-scene control     | Disabling character isolation restored two calls and 634,624 pixels despite a 0.1 m character range; configured FOV was retained   |
| FOV/screen edges and close-up        | Current source attribution remained valid; reduced work used the simple fallback where selected; no stale-mask or native failure   |
| Journal menu                         | NR was suspended and evaluations stayed 89,344; this did **not** establish retained-source evaluation                              |

The cold-empty checks and the quiet A-empty profiler window are distinct.
There were 24 conservative evaluations between them; within the profiler
window the count stayed 24. They are not silently reported as an entirely
zero-evaluation session. The sixteen exact A-empty transactions had zero
native executions, no guide source stage, and ordinary source/DLSS delivery.

Final native counters were **91,496 attempts, successes, evaluations and
output commits**, **43,987 successful stereo batches**, and **zero failures,
device removals, quarantines, stereo failures, validation failures or reset
failures**. There were 108 successful resets, 22 resource rebuilds and 11
initializations across intentional mode/FOV changes. Empty/re-entry windows
retained resources. GPU timing readback failures and backpressure waits were
zero. C's 82,856 caller history resets reflect its existing reset policy,
not a new temporal optimization. No Display/nvlddmkm warning or error was
found in the preserved process-start-to-near-shutdown event query.

Discovery is not free: the A-empty means were 0.053869 ms category capture
and 0.030784 ms early bounds. The previous producer's empty A guide cost
was 0.033838 ms; the new pass is absent on proven-empty eyes. This proves
work elimination, not a matched whole-frame saving across different runs.

## Task 5 cost and correctness

The main comparison held the camera and disabled global AI, with four
eligible actors. Reference/candidate/reference windows each resolved 300
profiler frames. Separate exact captures retained two native calls and
**2,007,040 evaluated pixels** in every leg. AI-off is not immutable native
input replay: jitter, rendering and other scene effects still advance.

GPU self-time means for **character mask work only**, in milliseconds:

| Held case                    | Reference 1 | Candidate | Reference 2 | Candidate minus mean reference |
| ---------------------------- | ----------: | --------: | ----------: | -----------------------------: |
| Feather off                  |    0.139734 |  0.162616 |    0.136672 |            +0.024413 (+17.66%) |
| Depth-aware feather radius 4 |    0.875550 |  0.328710 |    0.874062 |            −0.546096 (−62.42%) |
| Close-up crop, feather off   |    0.044073 |  0.045183 |    0.043995 |             +0.001149 (+2.61%) |

The feathered saving is 4.92% of an 11.11 ms frame budget, restricted to
this stage and workload. It does not establish a whole-frame or native
inference improvement. The close-up crop used 358×396 input per eye and
283,536 evaluated pixels. Delayed mask coverage was approximately 26%; it
is **not** a dense-support qualification or a current empty proof.

Other application stages remained separately measured: for the first held
reference, category capture was 0.061537 ms, early bounds 0.031858 ms,
NR depth guide 0.051275 ms, reduced selection 0.050656 ms and character
composition 0.083840 ms. The inclusive native stereo timer was 22.057789 ms.
Do not add nested scopes or D3D12 native queue times to these D3D11 means.
The prior session's 0.09 ms depth-guide baseline had different actor support,
evaluated area and clocks; it is not a controlled typed-copy speedup estimate.
Exact typed-copy correctness is established by the GPU fixture instead.

The initial unheld reference/candidate/reference series is retained but
excluded from paired conclusions: its candidate area changed from
2,007,040 to 2,114,560 pixels as actors moved. The three held comparisons
above have matching call counts and area in all three legs.

The candidate still launches the full mask output grid and skips expensive
pixel work through existing current GPU support. For the main no-feather
case, reference dispatch covered 632,448/640,256 pixels, versus 1,128,960
per eye for the candidate. Pending CPU bounds are compatible with consuming
the current GPU buffer, but do not identify a safe dense crossover. This
explains why small visible masks alone do not guarantee cheaper execution.

The [preceding qualification](nr-task4-5-qualification-20261002.md#offline-evidence-and-limitation)
preserves the isolated sparse/dense hardware pairs: dense support regressed
about 12.7% without feather and 3.8% with feather. The default reference
path remains appropriate. No additional discovery pass, CPU bounds wait,
per-ROI interop, native conditional dispatch or keepalive was introduced.

## Adversarial review and correction

The installed DLL exposed one DevBench defect: setting only
`experimentalGpuMaskSupport` was parsed but rejected as `nr_configure_noop`.
Its field was missing from the existing character-settings change detector.
The correction adds that comparison and documents standalone admission in
the registered input schema. It uses the existing ownership, settings key,
transition and reset paths; no parallel controller or render-loop work is
added. The change is entirely inside the DevBench bridge.

Live measurements used a recorded workaround: change the experiment together
with ROI hold frames, then restore the original hold value before capture.
Readback and exact execution receipts verified the desired dispatcher.
This does not count as a live test of the subsequently corrected one-field
request. The new source regression failed before the correction and passed
after it, checking every character override against admission detection.
Local feedback: `AUTO-20261002-181615198-DF554DD7`.

Review also challenged dirty-tile marking at R8's half-step rounding boundary.
Four new GPU cases test immediately below, at and above 0.5/255 and at 1/255,
including movement, departure, repeated empty and return through the existing
reference/sparse harness. All pass without a shader change. These supplement
54 sparse/dense coverage cases and the existing mono/stereo, alpha-hole,
feather, depth/occlusion, crop, poisoned initialization and finite/NaN tests.

Scope/correctness/robustness/DRY review retained current selected-draw bounds
as authoritative, conservative uncertain projection, source/capture/policy
identity and distinct CPU/GPU proof kinds. Authored support is not replaced
by an actor origin, joint box, sampled LOS or the other eye's rectangle.
The early proof reuses its plan; buffers survive their consumers; departed
tiles clear once; typed copies require validated source semantics and use
the existing copy helper/state guard. No new D3D resource or state mutation
is introduced by this final fix. SE/AE and VR share the control logic.

## Validation boundaries and remaining development

-   Completed GPU-empty proof, stale/pending/missing evidence, projection
    uncertainty and frozen-source reuse are covered by compiled production
    fixtures. They were not all observed live. Journal/menu suspension must
    not be relabeled as retained-source evaluation.
-   Depth occlusion and dense/thin/alpha-tested support are covered by GPU
    fixtures; the live camera cases are edge/disappearance/close-up evidence,
    not a controlled dense or occlusion performance campaign.
-   Universal SE/AE/VR compilation and mono/stereo fixtures pass; this machine
    provided VR gameplay only. No SE/AE gameplay result is claimed.
-   Equipment-only categories remain Task 11. Existing face/skin/hair selection
    was preserved, including conservative fallback when an anchor is absent.
-   Automatic sparse promotion, native cost modeling, compact allocation and
    final temporal/image-quality qualification are not granted by these results.
-   The new standalone DevBench-toggle fix has regression/build validation,
    but its new producer has not been installed or exercised in game. Its AIO
    is prepared for that narrow check when the user next chooses to install.

## Restoration and shutdown

NR, character, FOV and requested-configuration readbacks exactly matched
the initial snapshot. Evidence/profiler capture was disabled, owned freecam
released, global AI restored to on, and the original menu set restored.
`qqq` exited the exact game process at `2026-10-02T18:41:02.0979824Z`.
MO2 PID 31432 then closed normally through recovered session
`20261002T184134Z-nr-task4-5-followup-20261002-9c7ace15`; session and access
leases were released. No forced termination, installation or relaunch was
performed. Raw evidence, user outputs and shader caches are retained.

## Final offline validation and AIO

`pwsh tools/validate-local.ps1 -OutputDirectory build/validation/nr-task4-5-final-20261002`
built the universal Release DLL and executed all **220/220** controller,
GPU and shader entries, with zero failed, skipped, disabled or missing
tests. The final GPU log includes all four new half-step cases. Preset
generator regression, generated-preset verification, diff check and DLL
manifest verification also passed. The complete runner lasted 501.20 s.

**The full runner's terminal verdict is failed**, solely at its final
source-stability comparison: three unrelated untracked MGO preset `.7z`
archives appeared after compilation. They were preserved. Reconstructing
the current digest after removing only those three archive entries from
the in-memory status/untracked inventory exactly reproduces both the
initial snapshot and the compiled manifest's dirty digest. The tracked
diff and every other untracked input are unchanged; submodule identities
also match. This is an explicit qualified completion, not a rewritten
runner pass or an assumption that the archives were harmless.

`build/validation/nr-task4-5-final-aio-20261002/validation-followup.json`
retains both digests, the three archive hashes, original failure and passed
stages. `compiled-change.patch` and `compiled-source-files.json` preserve
the exact change compiled before these documentation updates. No source
change required a second DLL/test run.

Scoped pre-commit passed whitespace, line endings, clang-format and
Prettier; YAML, Gersemi and screenshot-schema hooks had no matching files.
The final code/test hashes still match the validated producer.

New producer Build ID:
`8c0267be060e89aacb3c5277e86a878e984285943d190be1b9eaf36ea2cbd98c`.
Compiled base: `4bd08e6f5a07c7efb28cabf078ce67f67532fde3`; dirty digest:
`80f793885d4c54b2e2b78dcec5c715ae88479b08d88ab6f75282b9aa0929d0f8`.
DLL: 31,229,952 bytes; SHA-256:
`88429ac36fd6a33780270393912efae59da4e79deadaf4d13249a1d2af1f5c94`.
DevBench is ON; Tracy and automatic deployment are OFF. The pinned neural
runtime remains 310.8. The compiled identity is not retroactively replaced
by the later documentation/commit identity.

Archive:
`dist/CSX_AIO-main-vr-nr-NR-Task4-5-DevBench-20261002-8c0267be060e.7z`.
Size: 199,068,106 bytes; SHA-256:
`df75d12e46603a699eb9a2a7cfc680ae69e3cbbb173591f55eb4e70adac1f298`.
Archive integrity passed, and all **385/385** extracted payloads matched
staging by size and SHA-256. DLL, PDB and manifest matched the producer;
the bundled NR DLL matched the pinned runtime hash. The adjacent receipt
records validation qualifications. No personal settings or shader cache
is included. This archive has not been installed or run in game.
