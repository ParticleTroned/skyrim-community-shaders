# NR colour multi-ROI live retest, 2026-09-15

## Identity and scope

The installed CommunityShaders producer was
`bc22df2891874eafe962688cf89f05f88df28c05`, Build ID
`05b75461cefdf3be8d1cf5d530cc6a0a4b29e6e6f8dfb89bdb5f625b9cdf795d`.
Its physical DLL SHA-256 was
`df3dd4b25be300a24ea65661fb51dd3aaad0876ce7a65223bb37c6e19c0e37ca`.
The authoritative exact hash and size are in the physical identity receipt.
The separately installed DevBench was
`1.18.1+pt.1.16.1.nr-observation-window.adb8b6c`.
The AIO had the bridge enabled and NeuralColor CORE 1-2-0, without
DevBench.dll, FOMOD, or shader cache.

Evidence is retained locally under
`build/validation/nr-live-20260915/retest-bc22df289-20260915T071942Z`.
The numbered journals contain the original typed direct MCP requests and
responses. No controller/HTTP connection or AIO validation campaign ran.
The physical identity receipt resolves enabled loose providers, Overwrite,
and unmanaged Data. The selected MO2 task profile, null-HMD observation,
and producer/DLL manifests are preserved there.

The scene was WhiterunDragonsreach, save `CSXTest01`, with FOV only at
0.95 and DLSS, 1512 by 1680 per eye. The user authorized this null-driver
assessment despite the missing managed pose provider. This does not
qualify physical headset presentation.

## Multi-ROI evidence

All accepted groups are distinct, API-v3 complete batches with matched
revision, generation, insertion, and source frame after the 16-frame
warm-up. Actual slot masks are used; two visible actors do not imply four
evaluation regions.

| Check                                                 | Result                                                                                                             | Evidence                                                                   |
| ----------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------- |
| Early, real Managed identity inference                | PASS: 80 complete groups, including both secondary slots                                                           | early-two-actors-inference.samples.assessment.json                         |
| Early, identity transport with slots 0,1,4,5 required | PASS: five four-region groups; maximum RGB error 0                                                                 | early-multi-transport-four-slot.samples.assessment.json                    |
| Late, identity transport                              | PASS: 63 complete groups, including nine four-region groups; maximum RGB error 0                                   | multi-all-transport.samples.assessment.json                                |
| Late, real Managed identity inference                 | PASS: 56 complete groups, including two four-region groups in that phase                                           | multi-all-inference-long.samples.assessment.json                           |
| Face-only inference/transport                         | PASS for the sampled single-region plans; no multi-region claim                                                    | multi-face-\*.samples.json                                                 |
| Two-eye committed images                              | Captured from hmd_submission; both actors visible in inspected early left image and earlier late left/right images | captures/early-two-actors-managed*\*.png and captures/multi-managed*\*.png |

Early four-region transport frames were 93214 through 93218 at revision 9. No incomplete-batch evictions or dropped colour measurements were
reported in these checks. Submitted images retain source frame, eye
bounds, format and colour space. Sequential captures are different live
frames; numerical transport success is not a lighting-quality verdict.

## Failures and source corrections

At frame 57957, a face-only to face/skin/hair transition requested category
mask 14 against captured mask 2. Right-eye preparation failed once.
The durable failure retained matching source/capture frame, generation 1,
slot 1, and the exact category-expansion reason. It remained visible after
recovery.

Category switches and strengths now latch by source frame across
authoring, capture, mask preparation and stereo finalization. A selection
edit waits for the next source frame. Retained-frame requests keep their
source selection; diagnostic-only settings remain independently editable.
Prepared-eye telemetry exposes effective category mask and strengths.
The captured-category subset check remains mandatory.

Right-eye current-copy timeouts also occurred. At the final character
snapshot the counters were left 0, right 141; the renderer's inference,
stereo, device-removal and quarantine failure counters stayed zero.
Conservative ROI fallback and retry diagnostics remained visible.
The former finalization loop performed left-eye CPU region planning
before reading the right eye under the same absolute 50 ms deadline.
Finalization now reads both buffers before either CPU plan. The deadline,
nonblocking Map, source-identity checks and conservative fallback remain.
The new maskRoiPlanningCpuMs diagnostic separates planning from GPU
readback waiting. Another installed-DLL run must establish whether this
removes the observed intermittent timeouts; it does not guarantee GPU
readiness under arbitrary load.

The earlier insertion-switch reproduction ran three cycles, 321 steps
and 162 NR receipts. Character preparation failures stayed 1 to 1
(previous DLL: three cycles added nine failures). Four receipts retained
a normal-DLSS/neural-evaluation-failed disposition for three unique
previous early frames despite successful per-eye evaluations. These
previous-frame transition receipts remain unresolved; recovery is not a
clean transition pass. Source review confirms that status reads preserve
the published disposition; they do not reclassify it from the current
settings. Both Feature 18 and center-blend success counts were one per
eye, but the published applied and committed masks were zero. This does
not identify the failed handoff or justify suppressing the fallback.

### Transition duration and backend lifetime

Each switch retained one distinct error frame: 63554, 63613 and 63670.
The next observed frame was a normal-DLSS bypass without an error; NR
was observed again at 63557, 63616 and 63672. One error frame appeared
in two responses with the same publication sequence. This is transient
recovery, not a persistent failure or a measured one-refresh hitch.

The respective post-switch main-thread status calls took 2462, 2353 and
2371 ms. The same run's log shows runtime initialization followed by
Feature 18 creation around two seconds later. These are operational
timings from an unqualified profiling environment, not headset-visible
durations or a controlled performance comparison. They nevertheless
identify cold backend recreation as a stall that needs correction.

Insertion-only changes now retain a healthy backend. The transition-frame
block and history invalidation remain; the renderer's history key includes
the insertion point and colour input epoch, and stereo history decisions
remain synchronized. Resource-key changes still wait for GPU idle and
retire incompatible slots before rebuilding. Master and multi-ROI changes
still retire the full backend, as do insertion switches with a latched
failure or quarantine. DevBench reports retirement separately from history
reset. This removes an unconditional teardown; the new installed build
must still demonstrate its actual recovery duration and fallback behavior.

## Baselines and limits

The first baseline matrix stopped at an explicitly rejected no-op
nr_configure call. Its journal is retained. The subsequent character-mode
matrix lacked enough fresh eligible inference samples and is incomplete.
The standard, character-disabled matrix then completed Raw, Managed,
Preserve Source and display-only A/B at both insertion points. Each phase
provided 12 fresh groups; Managed and Preserve Source numeric checks
passed, and A/B returned zero RGB error while inference continued. No
renderer or character-preparation failure counter increased during that
matrix. Raw observations are not passed to the Managed-mode validator.

Exposure capture is BLOCKED at both insertions: captures did not advance,
and the live shader differed from the selected original engine shader.
Community Shaders binds a replacement while retaining the original
engine selection pointer. Captured-exposure candidates and a production
colour-domain verdict remain unqualified.

The exposure binding hook now records the actual selected shader alongside
its engine selection, HDR producer, context, frame and capture epoch.
Draw-time validation accepts that exact replacement association and still
requires the live shader and AvgTex view. Original engine bindings retain
their existing validation. Source-frame exposure availability and resource
shape checks remain unchanged; successful live capture needs another run.

A controlled performance comparison was not run: the required neutral
probe and ownership epoch were unavailable. NR diagnostic timers were
retained without treating the colour/recording campaign as a benchmark.
Physical headset presentation, exterior candidate assessment, complete
manual-control visual review, and synthetic device-loss injection were
not qualified by this run.

Recording was stopped immediately at the user's instruction, at
1,352,104 ms, with 1,272 pose samples, 1,308 tracking samples and zero
reported unrecorded tail. The former 30-minute limit was NOT tested live.
The saved activity hash is
`6f5e9a74a0b02412511398c2f81b7b79f11578631f2c9b46db8482dc35a3375b`.
Recording and all owned screenshot work were confirmed inactive.
The attached game was left running.

## Validation of the source corrections

The category policy regression exercises same-frame expansion, next-frame
application, retained-source lookup, zero-strength transitions, resource
reset and independent debug controls. After commit 0f8247a04:

-   `pwsh ./tools/cmake.ps1 --build build/nrc --config Release --target character_settings_test character_mask_bounds_gpu_test`: passed.
-   `ctest --test-dir build/nrc -C Release -R '^(CharacterSettings|CharacterMaskBoundsGpu)$' --output-on-failure`: 2/2 passed in 3.31 seconds, including production HLSL and bounded WARP readback checks.
-   `python tools/nr-color/verify_assets.py` from the source root: passed.

The exposure selection regression rejects mismatched producer, context,
engine selection, frame, epoch and live replacement identities. After
commit 2a2f9d29b:

-   `pwsh ./tools/cmake.ps1 -S build/nrc-src/tests/neural_color -B build/nr-color-tests`: passed.
-   `pwsh ./tools/cmake.ps1 --build build/nr-color-tests --config Release`: passed.
-   `ctest --test-dir build/nr-color-tests -C Release --output-on-failure`: 16/16 passed in 20.96 seconds, including exposure policy, lifecycle, actual exposure HLSL on WARP, colour HLSL on WARP, and framebuffer restoration.

Scoped formatting hooks passed for both source corrections. The replacement
DLL build and its installed live verification are recorded separately in
the local build receipt; these dry tests do not establish live capture.
