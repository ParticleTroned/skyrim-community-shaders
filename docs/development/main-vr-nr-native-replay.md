# Task 2: bounded native NR replay

The original implementation checkpoint started from
`deb4b320d478524a33699f6af2c5a62997a05a62` on `main-vr-nr`.
The October 1–2 measurement checkpoint below uses compiled source
`042b7c05d6d9efb82b90de1bdef8929c5b0403b5`. Task 2 remains open;
the measurements do not promote a production cost model or default.

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

### Remaining Task 2 work

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
