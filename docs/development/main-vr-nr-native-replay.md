# Task 2: bounded native NR replay

The original implementation checkpoint started from
`deb4b320d478524a33699f6af2c5a62997a05a62` on `main-vr-nr`.
The October 1–2 checkpoint below uses compiled source
`042b7c05d6d9efb82b90de1bdef8929c5b0403b5`. The later
[C and input-storage checkpoint](#october-2-c-and-input-storage-checkpoint)
uses game source `9138db4b8766a88e2bc95352b2c7780d3fae5486` and a
separately identified standalone replay extension. **Task 2 is concluded
as a bounded measurement campaign; automatic cost-model adoption is
rejected.** The decision below supersedes the open status in historical
checkpoints. No production default is promoted.

## Task 2 conclusion — 2026-10-02

Task 2's specified outcome is to separate invocation, shape, capacity and
history costs without assuming proportionality. The measurements now
support that outcome and an explicit decision on their use. The task's
instruction to reject automatic adoption when stability or completeness
fails remains binding: a completed campaign can return an unqualified
production profile. The earlier open status kept later policy qualification
inside the measurement task; this conclusion assigns those gates below
without changing any failed or inconclusive result into a pass.

### Accepted findings

-   Small native regions have a substantial invocation cost. In the C
    confirmation runs, two stereo calls cost 4.3111–4.3169 ms and four calls
    over the same 65,536 pixels cost 8.6241–8.6286 ms. Full-eye C costs
    8.5894–8.6089 ms. The attained small-region lower sample envelope is
    about 4.29 ms for two calls; it is not a universal lower bound.
-   Allocation capacity predicts memory use better than native time in
    these tests. Compact C reduces DXGI process local usage from about
    967 to 683 MiB, with nearly unchanged cost, but changes native output.
    Compact allocation therefore fails the tested equivalence gate.
-   Output ownership and input context are distinct. Outside colour can
    affect owned output. A 128-pixel colour-space halo matches full-context
    output in the frozen A/B/C fixtures; 64 fails. The threshold, other
    placements and temporal quality remain unqualified.
-   Existing live telemetry resolves preparation/copy/composition stages.
    In the final C windows, stereo copies plus depth preparation total
    about 0.10–0.11 ms; final composition is 0.12–0.13 ms. Native evaluation
    is 9.50–10.46 ms. The clock domains and overlapping scopes cannot be
    added into a complete frame-cost model.
-   Pending current-frame bounds can cause real larger execution. All 80
    final retained transactions evaluate the full-eye area. Earlier live
    evidence includes a four-call plan with about 83% less area and no
    demonstrated D3D11 pass saving. Both outcomes are retained.

### Acceptance and decision record

| Task 2 deliverable                                    | Final disposition                                     | Evidence                                                                                                               |
| ----------------------------------------------------- | ----------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------- |
| Native A/B/C source and producer identity             | Complete                                              | Hashed colour/depth/motion/output bundles, verified provider/driver and physical game DLL identity                     |
| Independent size, shape, offset and call-count probes | Complete within tested one/two regions per eye        | A/B confirmation: 61 processes, 403 samples; C: 18 processes, 40 case results, 205 samples                             |
| Creation-capacity and output-equivalence comparison   | Complete; compact candidate rejected                  | 42 A/B/C storage/capacity processes, 336 samples, 18 bracketed comparisons                                             |
| Fixed output with independently varied input context  | Complete for frozen fixtures; no universal halo       | 60 processes, 480 samples, 48 bracketed comparisons with stable reference outputs                                      |
| Reset, cold creation and continuous-history cost      | Bounded probes complete; temporal quality unqualified | Static-reset/cold lanes and consecutive four-frame continuous/reset lanes; no looped history                           |
| In-game complementary measurements                    | Complete with explicit availability/stability limits  | Preserved quiet histories and 80 exact final stereo transactions; fallback and drift reported                          |
| Lower envelope, typical cost, spread and uncertainty  | Complete                                              | Per-run means, ranges, sample counts and retained raw values; slow regimes have no assigned cause                      |
| Automatic production cost profile                     | **Rejected**                                          | Drifting live baselines, unassigned native slow regimes, incomplete small-ROI stage coverage and quality qualification |
| Restoration and lifecycle                             | Complete                                              | Exact settings fingerprint restored, owned captures inactive, user-authorized normal Skyrim exit verified              |

Counts refer to separate named campaigns and retain their original source
identities. The exploratory failed 31×31 A probe is not included as a
successful timing or supported minimum. Synthetic mask occupancy remains
a CPU-composition proxy. No external GPU capture or moving-scene quality
qualification is claimed. Later stable references do not erase earlier
drift, helper interference, failed captures or driver events.

### Follow-up gates owned by later tasks

| Task  | Required follow-up before that task's production policy is accepted                                                                                           |
| ----- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 3C/3D | Qualify spatial context and overlapping read-only input independently of output ownership; 128 pixels is a fixture candidate only                             |
| 5/7   | Use measured valid input/support to qualify GPU work and shared transport; do not omit outside inputs merely because they are outside owned output            |
| 6     | Resolve current-frame readiness/fallback and repeat actual smaller-ROI live stage comparisons                                                                 |
| 8     | Qualify four-region planning, native instance capacity, memory and failure limits                                                                             |
| 9     | Obtain stable matched calibration after backend changes, investigate slow regimes as needed, and gate automatic cost adoption on held-out prediction evidence |
| 10    | Establish compact-adapter output equivalence and residency limits before using the observed memory saving                                                     |
| 12/13 | Qualify moving-scene history, temporal/stereo fidelity and final performance before promoting related defaults                                                |

The existing heuristic and conservative input initialization remain in
place. This conclusion requires no additional game session, deployment,
production instrumentation or change to model density/cadence. Reuse the
Task 2 runner for later qualification; do not infer a fitted cost profile
from this completion status.

Finalization also hardened the existing reporter: partial or unmatched
accepted sample sets retain descriptive case statistics but cannot produce
a paired timing delta or output-equality verdict. The new regression passed
with the other reporter tests (**25/25**); the unchanged measured standalone
binary retains its **12/12** input checks. Saved complete storage/context
pairs were revalidated under this guard. The final offline audit verifies
181 completed replay processes, 203 measured cases, 1,424 accepted samples,
2,276 retained binary crops and 66 bracketed storage/context comparisons.
Measured source hashes and physical DLL identity still match. Raw campaign
summaries remain immutable; the audit is retained separately under
`build/validation/nr-task2-conclusion-20261002`.

## Implementation and contracts

The optional DevBench `communityshaders.nr_replay` tool retains prepared
native colour, depth, motion and private output after successful NR, before
colour reconstruction and CSX composition. Full initialized rectangles,
source/crop/jitter provenance, auto-mask, real model edits and the existing
frame-evidence experiment are required. It preserves scaled guide grids
and feature mode. Requests are bounded by 32 frames, 30 seconds, two
pending GPU readbacks and 512 MiB of logical staging/CPU payload reservation
(twice row-packed bytes, excluding driver allocation padding). Failures,
missing consecutive frames and cancellation leave explicit incomplete
receipts. GPU staging is asynchronous; payload hashing/writes use a worker.
Completed bundles live beside the SKSE log under `NRReplay`.

The [standalone replay](../../tools/nr-replay/README.md) compiles the existing
production runtime and interop bodies with substituted platform includes.
Runtime admission and native submission remain shared. The developer tool
varies geometry, offset, aspect ratio, one/two-region calls, creation
capacity and reset/cold/continuous history. Capacity experiments preserve
source density and integer source/guide phase. Private output canaries
record the provider's actual footprint. Failed, bypassed, unwritten or
zero-edit evaluations do not qualify as fast results.

The reporter retains raw timings, counts, reset/creation state, resource
and evaluated rectangles, source identities, logical bytes and DXGI memory.
It separates cold, static-reset and continuous results and reports sample
counts, spread and descriptive 95% intervals. Comparisons reject changed
controls and require independent equally initialized temporal contexts.
Serially correlated samples are not independent population estimates.

Production A/B/C selection, native auto-mask, colour/Lighting preservation,
stereo publication, source-crop/jitter rules and defaults are unchanged.
No semantic mask is passed as a provider ControlMask. Occupancy experiments
use a labeled synthetic CPU composite proxy after native evaluation; they
do not measure the actual CSX GPU compositor or Preserve Source quality.
Four/eight regions remain deferred until the capacity task.

## Actual SDK and runtime inspection

Pinned Streamline SDK 2.14.1 exposes the core NR enum (1004), but this
checkout has no NR-specific header or loaded Streamline NR plugin. No
NR-specific feature requirement query was issued. Generic viewport or
duplicate-instance contracts are not treated as capacity guarantees.

The executable's real admission probe recognized the existing patched
runtime, version `310.8.0`, with SHA-256
`8270B350CD82DE5CE89806872CDD6B6A9249B80836B91BBEB3573470744CC206`.
The immutable local receipt is
`build/nr-replay-inspection-final-20260918/results.json`. This was an
inspection only: no model evaluation or GPU timing was performed.

## Original implementation validation

The standalone Release build passed, as did its nine input/decoder checks.
Capture WARP checks exercise formats, scaled guides, ownership, drift,
timeouts, cancellation, budget admission, readback and receipt completion.
Request tests exercise the actual extracted DevBench handler and its
developer/configuration guards. Reporter tests cover rejected/confounded
samples, intervals, dimensions, footprints and temporal pairing. Final DLL,
controller-test and package receipts are retained with the local AIO.
No HLSL was changed; this checkpoint makes no shader equivalence claim.

The Release universal DLL and controller targets built successfully with
`DEVBENCH_BRIDGE=ON`, `AUTO_PLUGIN_DEPLOYMENT=OFF` and shader tests disabled.
`ctest --test-dir build/nrb -C Release -L ControllerTests --output-on-failure`
passed **155/155**, including native WARP capture, request guards and all
18 reporter checks. The retained output is
`build/replay-controller-tests.log`. Scoped pre-commit and diff checks
passed. These results do not establish live Skyrim behaviour or timings.

## October 1–2, 2026 measurement checkpoint

Local raw evidence, scripts, input bundles and derived reports are retained
under `build/validation/nr-task2-20261001`. In-game results are in
`live-report.json`; the 61 completed offline confirmation runs and their
403 measured samples are in `offline-report.json` and `offline-report.md`.
Each run also retains the existing reporter's full JSON and Markdown.
Raw evidence is not versioned. The separate exploratory A matrix retained
23 completed cases (69 measured samples) and one failed minimum-shape case.

### Provenance

-   In-game producer Build ID:
    `d5d6574c84c9b2e80498a9c42fdfe0aa5782be1448309f0c9936c515f9b8390d`.
    DLL SHA-256:
    `e85458f8600f4bad972c4048aa175016863ef792161f415ef58c22a143868b3b`.
    Compiled source is `042b7c05d6d9efb82b90de1bdef8929c5b0403b5`,
    with dirty digest
    `630f3e90d685125aef618b43b6d5cd2ac7f99c620134cec0454053d594bdc205`.
    The physical enabled AIO, manifest and build receipt matched; no
    competing enabled loose DLL, Overwrite or unmanaged Data provider was found.
-   RTX 5070 Ti Laptop GPU, driver 610.88, null HMD. Native provider 310.8.0,
    SHA-256
    `8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206`.
    Replay verified the captured adapter LUID and driver version.
-   Initial 47-run confirmation replay executable SHA-256:
    `6b517cd1fb1d94a2111b336717dc999bd3f997c5e358a0af2d9dbc109ce127d5`.
    Each receipt retains exact runtime, interop, replay and SDK hashes.
    The final default-matrix minimum-shape exclusion followed these measured
    explicit-case runs; the later 14 clean-process repeats use the final
    executable identified below, not this initial compiled identity.
-   A manifest SHA-256:
    `7f38b7e99e67213239b28d336d0317c171d9ef6497fb959abd17b0145eee2290`.
    B manifest SHA-256:
    `f50a7df2eed1eab4ed7b8d6b7874633b693a53388f7ca4b9298185805ff9eecb`.
    Both contain four consecutive stereo native-input frames and real
    nonzero edits. A and B were captured separately; cross-route content
    is not identical. Every comparison within a route reuses its bundle.

### In-game C observations

The Bannered Mare view retained a single camera-matrix hash across all
45 observations. AI was disabled at the user's request. Two characters
were eligible under the current category/range settings. Nine bounded
300-frame captures ran baseline/single/baseline/multi/baseline/multi/
baseline/single/baseline. Character evaluation reduced total stereo area
from 2,257,920 to 1,648,640 pixels (26.98%), still using two calls. The
multi-ROI savings gate returned `insufficient_area_savings`, so this scene
did not provide a four-call in-game comparison. NR failures remained zero.

Baseline D3D11 NR pass means drifted from 13.626 to 23.097 ms. This prevents
a clean causal speedup claim from the live run. All timer histories and
exact native transaction observations are retained. D3D11 pass timing and
D3D12 native evaluation timing have different boundaries and are not added.
The bounded profiler histories identify capture/sample order, not exact
engine transaction IDs. Tracy was off; profiler and frame-evidence
instrumentation were enabled for their corresponding measurement windows.

NR, FOV, colour-evidence and profiler settings were restored before the
user closed Skyrim. AI remained off as requested. Debug log level was
changed manually by the user and was not restored through an unavailable
standalone setter.

### Frozen-input native cost

Skyrim was verified closed. Call-count/capacity confirmation used three
warmup iterations and eight measured samples per run, repeating forward
and reverse comparisons with intervening controls. The 17 additional B
axis runs used one warmup and three measured samples each. An SDK-only
diagnostic helper waiting for telemetry was still present during the first
47 runs. It was stopped before 14 further runs (112 samples), which
reproduced the call-count and capacity findings. The helper never loaded
NR or evaluated a model; the clean repeats retain that environmental
distinction. Native values below
sum actual evaluation timestamps across both eyes; ranges describe run
means, not confidence bounds. All 61 confirmation runs completed native
GPU retirement and SDK cleanup. Successful samples had nonzero edits,
fully written finite output and no writes outside their requested output
rectangles. This does not prove the footprint of private inference passes.

| Route | Configuration                          | Calls | Total evaluated pixels | Native mean range (ms) |
| ----- | -------------------------------------- | ----: | ---------------------: | ---------------------: |
| A     | Full control                           |     2 |              5,080,320 |          17.464–18.224 |
| A     | One 256×128 region per eye             |     2 |                 65,536 |            4.367–4.375 |
| A     | Two 128×128 regions per eye            |     4 |                 65,536 |            8.728–8.953 |
| A     | 126×126 region, full creation capacity |     2 |                 31,752 |            4.361–4.370 |
| A     | Same region, compact creation capacity |     2 |                 31,752 |            4.365–4.373 |
| B     | Full control                           |     2 |              5,080,320 |          17.566–18.554 |
| B     | One 256×128 region per eye             |     2 |                 65,536 |            4.369–4.381 |
| B     | Two 128×128 regions per eye            |     4 |                 65,536 |            8.739–8.982 |
| B     | 126×126 region, full creation capacity |     2 |                 31,752 |            4.365–4.381 |
| B     | Same region, compact creation capacity |     2 |                 31,752 |            4.366–4.372 |

Full and compact capacity preserve integer source/guide phase and content.
DXGI process local usage fell from approximately 1,310.7 to 683.2 MiB,
while native cost remained near 4.37 ms. Four calls used separate feature
instances and approximately 2,534.7 MiB: this is the cost of the tested
configuration, not an isolated guarantee about all instance counts.

B's width/height and equal-area aspect experiments stayed near 4.37–4.39 ms
over 16,384–65,536 stereo pixels. The synthetic 1%, 25% and 100% CPU
composition masks left native work unchanged at approximately 17.57 ms.
Cold creation measured 20.626 ms for the batch versus 18.057 ms inside
evaluation; CPU feature creation and diagnostic readback are separate.
Four-frame continuous/reset lanes completed, but do not establish temporal
quality or justify a reset-policy change.

The evidence supports a substantial small-region/per-invocation cost floor.
At 90 Hz, 4.37 ms is about 39% of an 11.11 ms frame budget before game
preparation and composition. Smaller allocations can save memory without
proportionally reducing inference cost. Fewer calls therefore merit priority
alongside evaluated area. These are isolated submissions with per-iteration
wait/readback, not pipelined in-game throughput or a fitted production model.

### Tooling repairs and retained failures

C input capture failed because its actual `R8G8B8A8_UNORM` colour/output
format was absent from the DevBench capture whitelist. The one-format
addition preserves raw bytes and is entirely behind
`DEVBENCH_BRIDGE_ENABLED`. A stereo WARP fixture verifies formats, row
packing, payload hashes and bytes. `NeuralReplayCapture`,
`NeuralReplayRequest` and `NeuralReplayReport` passed 3/3; the universal
DevBench DLL and AIO staging built successfully.

The replacement AIO Build ID is
`30a53c4d0e260f55ee5b1f0ef8aceeda17f15c59e4a01248e8474aa61236a090`,
DLL SHA-256
`1d9ed78a239041ca42984a9f5c87aa4cbdf186239e78a488380403c9880da7a8`.
Its compiled source remains `042b7c05d6d9efb82b90de1bdef8929c5b0403b5`
with dirty digest
`317191fcd7b1c47975ace55c1f703d7bc583cf217d78ae8123c246a50d78c657`.
Archive integrity, manifest identity and all 385 extracted file sizes and
SHA-256 values matched staging. It was not installed or tested in Skyrim.
Later standalone replay changes are not part of this game DLL.

Standalone replay initially could not select a parameter core because it
omitted the NGX bootstrap normally provided by Streamline. It now links the
pinned SDK, extracts the production project/engine identity, initializes
D3D11 NGX, and retains the unchanged production runtime's trust checks.
Failure messages are read after the failing call; SDK/library identity and
cleanup outcome accompany results. The nine input-contract tests passed.

Sandboxed SDK shutdown stalled inside NVIDIA telemetry IPC. A minimal
SDK-only reproducer showed `UninitializeTelemetry` waiting on a worker in
`WaitNamedPipeW`, without loading NR or evaluating a model. The identical
probe and subsequent replays exited normally outside the sandbox. Owned
stalled helpers were stopped; their logs, stacks and completed samples
remain distinct from the clean confirmation runs.

The exploratory 31×31 A probe returned native API success but failed GPU
completion and coincided with five `nvlddmkm` event 153 records. No timing
or minimum-size support is inferred from it. Further minimum-shape probes
were excluded, GPU recovery was verified with a fresh full-size control,
and the final default matrix requires explicit selection of experimental
minimum-shape cases. No production renderer size policy changed.
The final default-matrix validation completed 23 B cases with zero
minimum-shape cases and clean session closure. Its executable SHA-256 is
`f51067067be5e4f88c93cb814283ea90a344d84a0d2ce39ccf27656040fcd98f`;
all nine input-contract tests passed again. This one-sample-per-case run
validates the default selection and lifecycle, not performance uncertainty.

Automation's exact-timestamp correction already existed as local dev
commit `6ab345f`. Follow-up local dev commits `c6ca60c` and `b7f52cf`
recognize validated legacy render-scale status reads and preserve empty
GPU timer summaries/raw samples. Respectively 263 controller and 49
profiler checks passed; a three-fresh-frame live collector verified the
same process start time and restored profiler state. The installed plugin
cache was not rotated. The task's MO2 access lease could not be released
while the user's MO2 remained open; its diagnostic is preserved separately.

### Remaining work at the first checkpoint

This list describes the first checkpoint. The October 2 update below
completes C acquisition/repeats and finite input-storage comparisons.

1. After the user installs the verified AIO and starts Skyrim, capture and
   validate native C input, then repeat the same independent offline axes.
2. Compare poisoned versus initialized unused input storage at matched
   capacity/phase. Current canaries observe output writes only.
3. Obtain stable matched in-game baselines for preparation, copies,
   synchronization and composition, then relate them to native cost.
   The drifting C pass means cannot supply that decomposition reliably.
4. Preserve the minimum-shape failure as a limit of this experiment;
   qualify any proposed smaller production rectangle independently.
   Longer temporal and visual evidence remains necessary before changing
   reset/history policy. Short external GPU captures remain uncollected.

Task 2 and automatic cost-model adoption remain open. Four/eight regions
remain deferred to the later capacity task. No production defaults changed.

## October 2 C and input-storage checkpoint

The new C capture and fixed-input repeats reproduce the small-region
cost floor. Two calls over 65,536 stereo pixels cost about 4.31 ms; four
calls over the same area cost about 8.63 ms. Smaller allocation capacity
saves memory with essentially unchanged native time. It does **not**
preserve exact native output in the tested A/B/C cases. Independently,
changing only input pixels outside the requested native rectangles changes
output inside them on all three routes. These are failed output-equivalence
qualifications, not a reason to promote compact resources or partial copies.

Raw evidence remains local under `build/validation/nr-task2-20261002/`:
`context.json`, `live-report.json`, `bracket-comparisons.json`,
`restoration-verification.json`, `offline-C-report.json`,
`storage-report.json`, complete replay receipts, retained binary crops,
execution logs and source identities. A/B bundles remain in the preceding
`nr-task2-20261001/input-A` and `input-B` directories. The C measurements
contain 18 completed processes, 40 case results and 205 measured samples.
Storage/output controls add 42 completed processes and 336 measured
samples, with 18 before/variant/after comparisons across A/B/C.

### Producer and replay identity

-   The user-installed DevBench AIO is
    `CSX_AIO-main-vr-nr-9138db4b8-DevBench-20261002-4f47f218f65d`.
    Game source: `9138db4b8766a88e2bc95352b2c7780d3fae5486`.
    Build ID:
    `4f47f218f65df97c2d1047197c6c0681f415edcc9e867b5b2c458bc82750f814`.
    Physical DLL SHA-256:
    `fc97e9efd491b2d34c092208eea4710a286a1f673ac7cea8e1a93513382b2cf9`,
    31,202,304 bytes. Manifest, AIO receipt and enabled physical provider
    matched; no competing enabled loose DLL, Overwrite or unmanaged Data
    provider was found. The build's dirty digest
    `630f3e90d685125aef618b43b6d5cd2ac7f99c620134cec0454053d594bdc205`
    covers the preserved untracked Open Shaders note. DevBench was on,
    Tracy off; this is the universal SE/AE/VR build tested here in VR.
-   GPU/driver/provider match the preceding checkpoint: RTX 5070 Ti Laptop,
    610.88, NR 310.8.0, provider SHA-256
    `8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206`.
    The null HMD was configured for 1512×1680 per eye at 90 Hz. C native
    input is 1008×1120 per eye. The actual adapter LUID is in every receipt.
-   C capture manifest SHA-256:
    `e15a65274b8c256c41d1cdf9675b37f5fc9edad0c0ab47445a49f2b594c626be`.
    Four consecutive stereo source frames, 44630–44633, retain 32 payloads
    totaling 144,506,880 bytes. RGBA8 colour/output stays byte-exact. The
    captured control has 1,122,745 edited pixels and maximum absolute edit
    0.18823529. Producer PID 53244 started at
    `2026-10-02T06:33:23.3060996Z`; no later game instance is mixed into it.
-   C default and confirmation runs use the existing replay executable
    SHA-256
    `f51067067be5e4f88c93cb814283ea90a344d84a0d2ce39ccf27656040fcd98f`.
    The standalone storage extension was compiled separately from source
    `9138db4b8766a88e2bc95352b2c7780d3fae5486` plus the replay changes;
    its executable SHA-256 is
    `e467a717c9212653fb2ceb875d11c127ca5c5b9f24b1a5cbb7206120db3de311`.
    Every receipt preserves exact replay/runtime/interop/SDK source hashes.
    The shared native runtime and interop implementations are unchanged.
    This extension is not part of the game DLL or AIO.

### Live C comparison and restoration

Three eligible characters were in view in the Bannered Mare, with AI off
and one camera-matrix hash across 45 observations. Nine 300-frame windows
retain 2,700 measured frames in the order B0/S1/B1/M1/B2/M2/B3/S2/B4.
Baseline full-eye execution covers 2,257,920 stereo pixels in two calls;
single execution covers 2,150,400 (4.76% less), also in two calls. Sampled
multi execution covers 385,024 (82.95% less) in four calls. Early pending
GPU bounds can change a contemporaneous planner result; these figures
come from actual execution rectangles, not the latest plan fields.

| Window | Configuration | D3D11 NR pass mean (ms) | Native counter-interval mean (ms) |
| ------ | ------------- | ----------------------: | --------------------------------: |
| B0     | Full-eye      |                 13.2586 |                            11.728 |
| S1     | Single        |                 13.0932 |                            12.549 |
| B1     | Full-eye      |                 13.9727 |                            13.310 |
| M1     | Multi         |                 10.7721 |                             8.962 |
| B2     | Full-eye      |                 10.4672 |                             9.543 |
| M2     | Multi         |                 10.4313 |                             8.975 |
| B3     | Full-eye      |                 10.2261 |                             9.595 |
| S2     | Single        |                  9.5902 |                             9.073 |
| B4     | Full-eye      |                  9.8772 |                             9.291 |

The first multi bracket has about 25% baseline drift and does not establish
a causal speedup. Later M2 is about 0.085 ms (0.8%) slower than the mean
of its two adjacent D3D11 baseline means, despite about 83% less evaluated
area. Its native counter-interval mean is about 6.2% lower. These boundaries
and intervals differ: do not add them or subtract them to claim exact copy,
preparation, synchronization or composition cost. The bounded GPU history
has capture/sample order, not exact engine transaction IDs. Sparse native
frame observations retain their own transaction identities separately.
Pending per-copy GPU timing is unavailable; CPU enqueue time is not GPU
duration. Zero observed CPU backpressure waits does not prove no GPU waits.

All NR/FOV/colour settings were restored with exact requested-configuration
equality and fingerprint match. Profiler and capture writer were inactive.
NR failures, device removals, quarantines and stereo failures stayed zero;
the compiler stayed idle at 13 completed tasks and zero failed. AI remained
off as requested; the user-enabled Debug log level was not changed.

The user closed Skyrim. A subsequent game instance (PID 64300, local start
09:09:13) was detected before confirmation dispatch; that attempt submitted
no repeats. Work waited for the user's closure confirmation. All resumed
offline runs checked for an absent game before dispatch. No game shutdown,
restart, DLL installation or launch was performed by this continuation.

### Isolated C native cost

The 23-case default matrix used one warmup and three measured samples per
case. Seventeen confirmation processes used three warmups and eight
measured samples, with interleaved references and reverse-order repeats.
The following ranges are run means; sample ranges describe observed
variation, not population confidence or production tail guarantees.

| Configuration                                    | Calls | Stereo pixels | Run mean range (ms) | Individual sample range (ms) |
| ------------------------------------------------ | ----: | ------------: | ------------------: | ---------------------------: |
| Full-eye, nine references                        |     2 |     2,257,920 |       8.5894–8.6089 |                  8.576–8.627 |
| One 256×128 per eye, two repeats                 |     2 |        65,536 |       4.3111–4.3169 |                  4.293–4.328 |
| Two 128×128 per eye, two repeats                 |     4 |        65,536 |       8.6241–8.6286 |                  8.600–8.658 |
| 128×128 per eye, full capacity, two repeats      |     2 |        32,768 |       4.3105–4.3165 |                  4.292–4.344 |
| Same valid pixels, compact capacity, two repeats |     2 |        32,768 |       4.3217–4.3250 |                  4.299–4.348 |

The observed lower sample envelopes are about 4.29 ms for two small calls
and 8.60 ms for four; the typical run means are about 4.31 and 8.63 ms.
These are attained values in this campaign, not a fitted universal floor.
At 90 Hz, 4.31 ms consumes about 39% of the 11.11 ms frame budget before
other rendering. Full/compact small-region DXGI process local usage was
967.4/683.2 MiB; four calls with separate instances used about 1,848.2 MiB.
The compact result is a memory/cost observation, not an output-equivalence
pass. Allocation size and evaluated area are insufficient cost predictors.

Width/height, aspect and offset cases stayed near 4.30–4.33 ms. Synthetic
1%, 25% and 100% CPU-composition occupancy retained native means near
8.59 ms. Cold creation measured 10.514 ms for the GPU batch versus
8.594 ms inside evaluation. Continuous/reset four-frame lanes completed,
with measured means 8.723/8.588 ms after one warmup; they do not qualify
temporal quality or a changed history policy.

All C replay cases completed GPU retirement and SDK shutdown. Qualifying
samples had nonzero edits, fully written finite output and no writes
outside the requested output rectangles. Each iteration waits/readbacks;
these are isolated submissions, not pipelined whole-game throughput.

### Input storage and output equivalence

The existing standalone runner now offers explicit captured/zero/finite
outside-input controls with full backing capacity and fixed valid
colour/depth/motion rectangles. Valid pixels, density and integer guide
phase stay exact. SHA-256 receipts verify this for each input and sample.
Capacity cases also retain those receipts. Both axes save private requested
output crops. Hashing, uploads and output readback/writes are outside native
evaluation timestamps. No production or bridge-off instrumentation changed.

Fourteen processes per route compare full/compact capacity twice, then
captured/zero/captured/finite/captured/finite/captured/zero/captured. Each
uses three warmups and eight measured samples. All 42 runs close cleanly;
all outputs are finite, written and deterministic across measured samples.
Adjacent captured-input references are bitwise identical, including the
reverse-order repeats. All 18 variant comparisons change native output.

| Route | Variant                       | RGB pixels changed | Maximum absolute RGB difference |
| ----- | ----------------------------- | -----------------: | ------------------------------: |
| A     | Compact capacity              |            98.410% |                        0.042969 |
| A     | Zero outside inputs           |            99.991% |                        0.046875 |
| A     | Finite pattern outside inputs |            99.997% |                        0.039063 |
| B     | Compact capacity              |           100.000% |                        0.062500 |
| B     | Zero outside inputs           |            99.975% |                        0.062500 |
| B     | Finite pattern outside inputs |            99.499% |                        0.046875 |
| C     | Compact capacity              |            94.064% |                        0.035294 |
| C     | Zero outside inputs           |            99.966% |                        0.066667 |
| C     | Finite pattern outside inputs |            99.963% |                        0.035294 |

Both repeats have identical output differences. These are decoded raw
native RGB values, not perceptual or exposure-normalized comparisons;
separately captured A/B/C inputs are not cross-route quality references.
Differences persist at 32- and 48-pixel insets, not only at crop edges.
The controls modify colour, depth and motion outside their valid rectangles
together, using finite values only. No NaN/Inf input was submitted.

This establishes dependence on input content outside the requested
rectangles. It neither identifies the contributing resource nor measures
the exact private read/compute footprint. The cause of compact-capacity
output changes is not separately established. Output ownership, requested
evaluation area and required readable context must remain distinct.

Some outside-input variants also enter slower timing regimes. C finite
repeats measured 4.306 and 9.598 ms with identical outputs; A zero/finite
runs measured 11.80–12.23 ms; B zero/finite runs measured 6.80–9.57 ms.
Adjacent captured-input controls remained near 4.30–4.32 ms. The cause is
unknown. Preserve these samples; do not fold them into a universal area
fit or silently select the fastest repeats. The Windows event query from
`2026-10-02T08:30:02.6142244Z` found no `nvlddmkm` events during the resumed
confirmation/storage campaign (`replay-driver-health.json`).

### Validation and remaining qualification

The standalone Release build succeeded with existing external CommonLib
warnings. CPU-only input tests passed 11/11, including independent expected
bytes/hashes for all six supported formats, nonzero origins and scaled
guides. Reporter tests passed 23/23, including changed valid pixels,
missing/malformed crop evidence, duplicate/unmatched slots and storage
policy changes. The reporter separates exact crop equality from timing
comparability and from perceptual/temporal quality. Earlier receipts
without crops remain explicitly unavailable for output equality.

Commands:

```powershell
python tools/nr-replay/test_input.py build/nr-replay-storage-9138db4b8/Release/csx_nr_replay.exe
python tests/neural_color/replay_report_test.py
```

Configure/build receipts are `storage-build.log` and
`storage-final-build.log`; scoped formatting
and diff checks accompany the extension. No game DLL, shader or AIO was
rebuilt for these storage controls; no SE/AE gameplay claim is made.

Task 2 now has C capture/repeats and A/B/C storage/output evidence. It does
not yet qualify an automatic production cost model. The next priorities are:

1. Establish the required readable context with fixed output ownership,
   separating colour/depth/motion changes and testing increasing input
   context. Retain initialized backing data until the bound is proven.
   Qualify compact capacity independently; current output differences fail
   the proposed equivalence gate.
2. Obtain stable, transaction-attributed live preparation/copy/sync/composite
   evidence before claiming additive cost decomposition or gameplay savings.
   The present live windows document the symptom but do not supply that fit.
3. Retain minimum-shape, slow-regime and temporal limitations. Longer
   temporal/visual assessment and any needed short external GPU capture
   remain uncollected. Four/eight-region qualification stays with Task 8.

No NR default, history policy, production allocation, copy extent or
semantic mask was changed by this campaign.

## October 2 fixed-output context and live stage follow-up

Evidence root: `build/validation/nr-task2-context-20261002`. The maintained
standalone runner gains explicit resource/halo cases, using the existing
rectangle expansion and outward guide mapping helpers. Input receipts
prove unchanged valid and preserved bytes. Output ownership, evaluation
rectangle, creation/backing capacity, pixel density, phase and static reset
stay fixed; only finite data outside the preserved context changes. This
is a developer executable change, with no production instrumentation.

### Fixed-output context evidence

The standalone executable is
`build/nr-replay-context-9138db4b8/Release/csx_nr_replay.exe`, SHA-256
`c378e089ad9ba66868ca1b02d9f7af4c6f668bfd9e5009f932eb042a9cb512fb`.
It retains the production Runtime/Interop and SDK hashes in every receipt;
its compiled `main.cpp` SHA-256 is
`7d7c72c0f8f6813d038a61fc8bd83d2f336d96066489533f6511da4b0910f651`.
The previously recorded A/B/C native bundles, provider 310.8.0, driver
610.88 and RTX 5070 Ti Laptop GPU are unchanged. This executable is
separate from both previously measured replay executables and the game DLL.

`context-screen-plan.json` and `context-confirm-plan.json` completed
**60 processes, 480 measured samples, three warmups per process and 48
bracketed output comparisons**. `context-report.json` retains every run,
rectangle, policy, timing range, input proof, output comparison and adjacent
reference identity. All runs completed GPU retirement and SDK shutdown;
output was finite and fully written with no writes outside ownership.
All measured outputs were deterministic within each run, and both enclosing
captured-input references matched bitwise. Binary crop hashes were checked
against the actual retained files. Skyrim absence was checked before each
process in the normal driver-access context.

| Route | Output per eye (x, y, width, height) | Colour backing | Guide backing |
| ----- | ------------------------------------ | -------------- | ------------- |
| A     | 1368, 1122, 126, 126                 | 1512×1680      | 1008×1120     |
| B     | 1353, 1128, 126, 126                 | 1512×1680      | 1008×1120     |
| C     | 879, 729, 128, 128                   | 1008×1120      | 1008×1120     |

Both eyes use the listed rectangle. Halos are in native colour pixels,
clamped at image boundaries and mapped outward to the guides. These
near-right-edge crops do not test an unclipped halo in every direction.

| Treatment outside preserved context                 | A         | B         | C         |
| --------------------------------------------------- | --------- | --------- | --------- |
| Colour only, zero, no halo                          | Different | Different | Different |
| Depth only, zero, no halo                           | Exact     | Exact     | Exact     |
| Motion only, zero, no halo                          | Exact     | Exact     | Exact     |
| All inputs, 32-pixel halo, zero and finite pattern  | Different | Different | Different |
| All inputs, 64-pixel halo, zero and finite pattern  | Different | Different | Different |
| All inputs, 128-pixel halo, zero and finite pattern | Exact     | Exact     | Exact     |
| All inputs, 512-pixel halo, zero                    | Exact     | Exact     | Exact     |
| Full preserved backing, zero policy                 | Exact     | Exact     | Exact     |

“Exact” means bitwise equality of both raw native owned-output crops over
all eight measured iterations, not perceptual or temporal qualification.
Additional C finite-pattern probes separately changing colour, depth and
motion reproduce the respective Different/Exact/Exact result. A/B also
match at 256 pixels for both policies and at 512 for finite patterns.
C's tested 16-pixel halos differ for both policies. Its 1024 and 16384
halos cover all input, leave zero changed pixels and reproduce the exact
reference. The full-backing finite-pattern C control also matches.

Thus **128 is the smallest tested passing halo for these fixtures; 64
fails**. The exact threshold between them is unmeasured. This bounds the
observed output dependence, not the provider's physical read/compute
footprint. It does not permit dropping depth/motion in production, changing
capacity, reducing density, or adopting a universal 128-pixel margin.
There is only one frozen source and output placement per route, with reset
on every evaluation. Other shapes/origins, moving histories, new scenes,
preservation/composition and stereo visual quality need separate checks.

Timing remains a separate limitation. Captured-reference run means are
4.3100–4.3209 ms (A), 4.3018–4.3101 (B) and 4.3014–4.3164 (C).
The matching 128-pixel zero/finite means are 4.3152/9.5756 ms (A),
4.2982/12.2027 (B) and 4.3070/6.9350 (C). Even A's full-backing control
with **zero changed input pixels** measures 9.4495 ms, between references
near 4.31 ms. The slow regimes therefore cannot be attributed solely to
changed input values, and their cause remains unestablished. Preserve all
samples; equal output does not qualify a timing fit or new production policy.

### Live preparation, copies and composition

The user-returned Skyrim instance was PID 36428, started at
`2026-10-02T09:15:18.4558871Z`. It used the same installed AIO/source,
Build ID `4f47f218f65df97c2d1047197c6c0681f415edcc9e867b5b2c458bc82750f814`
and DLL SHA-256
`fc97e9efd491b2d34c092208eea4710a286a1f673ac7cea8e1a93513382b2cf9`
(31,202,304 bytes). `final-dll-verification.json` matches the sole enabled
physical provider, adjacent manifest, AIO receipt and runtime producer,
including original compile source/dirty digest. No competing loose provider
was found. Profile: `Codex Task - 20260919t055629z-tracy-guardian-main-vr-1bf5803a`.
Scene: Bannered Mare; SteamVR null HMD, 1512×1680 per eye at 90 Hz;
C native input 1008×1120 per eye, DLSS preset 1/quality 3 and framegen off.
All 13 shader tasks were complete, with no active compilation or failures.

Global AI was initially on and was switched off once, with the console
reporting “All AI Processing is Off”; it remains off as requested. The
initial `ctx-b0`, `ctx-s1` and `stage-s1` pilot precede that change and are
excluded from the AI-off comparison. Nine quiet 300-frame windows ran
B–M–B–M–B–S–B–S–B, where B disables character selection, M enables it with
multi-ROI, and S enables single-ROI selection. C mode, settings, camera
and density stayed fixed. The performance guard proved no standalone
temporal probe was registered; profiler/frame evidence remained enabled
throughout, so this is a DevBench instrumentation configuration.

`quiet-window-report.json` retains the bounded timer histories separately
from sparse exact execution observations; they are not falsely joined into
per-frame samples. NR-pass run means were B 11.4357–11.7090 ms,
M 11.2716/11.5810 and S 11.5200/11.5418. Baseline drift prevents interpreting
the small differences as a stable production saving.

Five additional B–M–B–S–B windows retained **80 unique stereo transactions**
through the existing screenshot service: sixteen consecutive 16×16 tiles
per eye per window, 32,768 logical staging bytes per window. These tiny
captures retain completed D3D11/D3D12 stage timestamps; their timings are
reported separately from quiet windows. `live-stage-report.json` preserves
every exact transaction and availability reason. Shared category/bounds
handles are deduplicated; eye-local work is summed once per stereo frame.

| Stereo stage, mean across each 16-frame window | B reference range (ms) |  M (ms) |  S (ms) |
| ---------------------------------------------- | ---------------------: | ------: | ------: |
| Native evaluate, D3D12 queue                   |         9.5002–10.2879 | 10.0822 | 10.4557 |
| Colour copy, D3D11 GPU                         |          0.0235–0.0254 |  0.0214 |  0.0236 |
| Depth guide, D3D11 GPU                         |          0.0395–0.0425 |  0.0421 |  0.0434 |
| Motion copy, D3D11 GPU                         |          0.0194–0.0212 |  0.0209 |  0.0215 |
| Output copy, D3D11 GPU                         |          0.0215–0.0235 |  0.0230 |  0.0238 |
| Category capture, D3D11 GPU                    |           Not executed |  0.0416 |  0.0412 |
| Early bounds, D3D11 GPU                        |           Not executed |  0.0198 |  0.0193 |
| Character mask, D3D11 GPU                      |           Not executed |  0.0948 |  0.0991 |
| Pre-upscale selection, D3D11 GPU self time     |          0.0498–0.0538 |  0.0020 |  0.0021 |
| Final source composite, D3D11 GPU              |          0.1191–0.1251 |  0.1240 |  0.1272 |
| Native input preparation, CPU inclusive        |          0.0248–0.0278 |  0.0264 |  0.0267 |
| Native output commit, CPU inclusive            |          0.0141–0.0176 |  0.0168 |  0.0188 |
| Character preparation, CPU inclusive           |           Not executed |  0.0726 |  0.0604 |

The active colour mode is `legacy_raw`: colour preparation/reconstruction
and provider control-mask copy scopes are explicitly unavailable because
they were not entered. No explicit CPU wait was issued in the 80 retained
transactions. That does not prove absence of GPU queue synchronization.
The legacy D3D11 NR self interval is 10.0353–11.0367 ms across these
windows. Do not add it to D3D12 evaluation, subtract the clocks to invent
an independent synchronization cost, or add overlapping CPU inclusive
intervals. These data quantify the observed stage costs, not complete
un-instrumented gameplay or an automatic additive cost model.

All 80 retained transactions executed **two calls and 2,257,920 pixels**.
Both character eyes in all 32 M/S transactions reported early bounds
pending and used full-eye geometry fallback; all 40 sparse character-eye
observations in the quiet treatment windows did likewise. Four eligible
actors were observed per eye. Cumulative counters also contain some ready
bounds outside these samples: this is not a claim that readiness never
occurs. The samples establish real full-eye execution, not merely stale
planning telemetry. They do not measure a successful smaller multi-ROI
workload. Task 6 readiness/snapshot work must address that qualification.

### Exclusions, restoration and validation

The local stage wrapper initially used a PowerShell automatic variable and
a stale/null native exit-code check. Its bounded profiler capture 13 still
completed; the result is preserved. Five later `stage-*` windows selected
Foveated instead of C, whose unavailable FOV left NR inactive. Their
missing execution evidence caused exclusion, not an inferred zero cost.
After correcting the setup and adding readiness/exact-route checks,
`stageC-*` captures 19–23 supplied the qualified table. A route-label
assertion initially rejected the valid first capture (`submit`, not `C`);
its existing result was recovered and validated without rerunning it.
No failed or incomplete capture is silently promoted to a timing sample.

The installed controller also lacks semantic adapters for legacy valid
render-scale/console payloads. Raw receipts are retained with local toolkit
feedback `AUTO-20261002-093817396-A63165C7`; no plugin cache or production
code was changed. The final `qqq` receipt proves dispatch only; separate
process-exit evidence establishes completion.

NR settings and colour experiments were restored exactly to fingerprint
`595a017e750e49a55dc1cf017f2741fd`; profiler and frame evidence were disabled
with no owned capture active. NR reported zero failures, no quarantine and
successful retirement. At the user's explicit instruction, normal engine
`qqq` shut Skyrim down; the exact PID/start identity and exit were verified
without forced termination. `game1-closed.json` records this, and every
subsequent replay checked game absence. No game installation or restart
occurred. The UTC-filtered System log contains no `nvlddmkm` or Display
4101 fault in this window; an informational Display 4127 Auto HDR suggestion
is retained separately in `driver-health-assessment.json`.

Standalone Release build passed with the existing CommonLib warnings.
Input tests passed **12/12**, covering selected resources, clipped context,
odd guide ratios and preserved-byte hashes as well as all supported formats.
Reporter tests passed **24/24**, including halo/policy/hash contradictions.
The compiled source hashes match the measured executable. Scoped pre-commit
and diff checks passed. Build, test and hook logs are retained in the
evidence root; no game DLL or shader was rebuilt.

At this checkpoint Task 2 remained open for broader output/context,
history, smaller live workloads and stable cost qualification. The
[final conclusion](#task-2-conclusion--2026-10-02) assigns those production
gates to their implementation tasks and concludes the measurement campaign
with automatic cost adoption rejected. Compact creation and a universal
halo remain unqualified. No production default is promoted.
