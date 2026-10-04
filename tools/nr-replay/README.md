# Native NR replay

This optional developer executable replays captured native NR inputs through
the production `Runtime.cpp` and `D3D12Interop.cpp`. It does not install a
game DLL or change a production default. Its GPU timings cover the native
provider and its submission, not complete A/B/C rendering or CSX colour
reconstruction. Raw native output differences are not Preserve Source
quality assessments.

The standalone target substitutes dependency includes and extracts the exact
existing path, version and resource naming helpers. Runtime admission,
caller-path proxy, parameter-core selection and teardown are unchanged.
Source and SDK header hashes accompany every result. The executable uses
its own `Data/Shaders/Upscaling/Streamline` directory; `--runtime` may copy
the exact captured provider there, but never replaces a different DLL.
Before native admission, the standalone tool initializes the pinned NGX
SDK on its D3D11 device using the production Streamline project identity.
This loads the driver parameter core that Streamline supplies in game;
the unchanged production runtime still validates its path, signature,
version and hash. Bootstrap status and the SDK library hash are retained.
The SDK is shut down after native GPU work and features have retired.
Run native GPU qualification with normal local driver IPC and telemetry
access rather than a restricted filesystem sandbox. October 1 SDK-only
and October 4 C-layout checks stalled in NVIDIA telemetry during SDK
shutdown inside the sandbox. With the same October 4 executable and
contexts, native, original-schedule, N1 and shared-N2 repeats completed
outside it. This is observed environment sensitivity, not a guarantee for
every configuration. Preserve failed receipts and cleanup diagnostics
separately from completed GPU samples; retain the real SDK shutdown rather
than bypassing it when a deadline expires.

Configure and build from the repository root. `CSX_NR_DEPENDENCY_ROOT` may
point to another local checkout with the same populated external SDKs:

```powershell
pwsh ./tools/cmake.ps1 -S tools/nr-replay -B build/nr-replay `
  -G 'Visual Studio 18 2026' -A x64 `
  -D 'CMAKE_PREFIX_PATH=<existing-vcpkg-installed>/x64-windows-static-md'
pwsh ./tools/cmake.ps1 --build build/nr-replay --config Release
```

Inspect the actual runtime admission without evaluating a model:

```powershell
./build/nr-replay/Release/csx_nr_replay.exe --inspect `
  --runtime '<physical-provider>/nvngx_dlssnr.dll' `
  --output build/nr-replay-inspection-unique
```

The input is a completed `csx-nr-replay-input-v1` native capture. Screenshot
PNGs alone are insufficient: the bundle needs matching full initialized
colour, depth, motion and native output resources, source frames, source
and guide origins/jitter evidence, runtime identity and driver identity.
Every binary resource is hashed and tightly row-packed in its original
DXGI format. The capture must show a nonzero native edit. No synthetic
input is accepted as runtime performance evidence by the supplied workflow.
Capture preserves `R8G8B8A8_UNORM` colour/output on the reduced-resolution
route as well as the supported floating-point formats; it does not convert
or reinterpret those pixels. This readback exists only in DevBench builds.

Install the matching DevBench-enabled AIO, enable developer mode and the
existing NR `captureFrameEvidence` experiment, then use the dynamically
registered `communityshaders.nr_replay` tool:

```json
{ "action": "capture", "frames": 1, "timeoutMs": 30000 }
```

Poll with `{"action":"status"}`. Cancellation requires the returned
`requestId`. Completed bundles are under `NRReplay` beside the SKSE log;
they are outside MO2's virtual Data tree. The tool does not change NR
settings. Capture full initialized auto-mask rectangles with real model
edits; partial character rectangles are rejected. Capture each A/B/C route
separately with matched settings. The replay selects bounded native regions from that complete source,
retaining each route's provenance. Native input capture still accepts one
full initialized context per actual eye; multi-region execution evidence is
reported separately and is never relabeled as extra eyes.

Capture reserves at most 512 MiB of logical staging plus CPU payload
(twice the tightly packed bytes); driver allocation padding is excluded.
This limits high-resolution stereo bundles to very few frames. Request
one frame for static throughput first. A short consecutive bundle can
exercise history handling with explicit small sample counts, but cannot
establish moving-scene temporal quality. Capture overhead is excluded from
the standalone provider measurements.

```powershell
./build/nr-replay/Release/csx_nr_replay.exe `
  --manifest '<capture>/manifest.json' --output build/nr-replay-input-check-unique `
  --validate-input
./build/nr-replay/Release/csx_nr_replay.exe `
  --manifest '<capture>/manifest.json' --output build/nr-replay-storage-check-unique `
  --validate-storage
./build/nr-replay/Release/csx_nr_replay.exe `
  --manifest '<capture>/manifest.json' --output build/nr-replay-results-unique `
  --runtime '<physical-provider>/nvngx_dlssnr.dll' `
  --warmup 3 --samples 8 --seconds 180
python tools/nr-color/replay_report.py --help
```

Output directories must be new. `--case ID` selects a bounded case, such as
`calls-two`, `capacity-compact` or `temporal-continuous`. Maximums are 64
measured samples, 32 warmup samples, 600 seconds, a 512 MiB input bundle
and 2 GiB of logical replay textures. Native calls cannot be preempted;
the deadline is checked between submissions, and readback/teardown use
bounded waits. Private provider allocations are observed through DXGI
process memory accounting, not included in the logical texture bound.
Minimum-shape experiments require an explicit `--case minimum-shape-N`;
the default matrix excludes them. On the recorded 310.8.0 provider and
610.88 driver, the 31x31 A probe failed its GPU completion check and produced
driver events after 23 other cases completed. That is a failed experiment,
not evidence of a general supported minimum. Do not repeat it as routine
measurement or mix it into a running game's GPU workload.

For an explicit `capacity-full` or `capacity-compact` case,
`--capacity-size 128|256|512|768` selects the same crop dimensions in both
storage layouts. The default is 128. These cases initialize and evaluate
the entire selected crop without resampling; compare equal-sized cases
in separate full/compact/full processes. A larger requested size is clipped
to the captured grid by the existing geometry builder; inspect actual
dimensions in the result. `--capacity-temporal` consumes consecutive captured
frames with a reset on every evaluation, preserving stateless C. Supply
at least `warmup + samples` captured frames. A short sequence establishes
only those frames' output equality, not general temporal quality. Do not
overlap qualification with a game, compilation or another GPU campaign.

`--alternate-output-sentinel` complements the deterministic RGBA8 output
pattern. Use a separate identical-input replay to investigate a finite
sentinel collision, comparing the retained raw outputs and both patterns.
A matching byte pattern is ambiguous; it does not itself prove a missing
provider write. Both ambiguous matches and nonfinite output remain rejected
by the report. The switch does not add inference or silently waive a failed
sample, and float formats retain their existing NaN sentinel. This probes
output writes, not the backend's complete input-read footprint.

`context_probe.py` accepts the same switch and pins it in the immutable
plan. Every admitted native result and footprint must agree with that
selection. Earlier failed runs remain separate evidence; choosing a new
marker does not turn their ambiguous samples into valid measurements.

Static throughput repeats one frozen frame and resets every evaluation.
Cold creation releases features after a GPU-idle proof each iteration.
Temporal pairs consume the same consecutive captured sequence with fresh
equally initialized contexts, once continuously and once resetting every
frame. If there are fewer frames than warmup plus samples, the lane is
unavailable. Reduce the explicit sample/warmup counts for a short bundle;
the tool never loops a short temporal sequence or fabricates frames.
Temporal outputs are retained for a separate quality assessment.

Width, height, offset, aspect ratio and one/two-region call count vary at
fixed creation capacity. Full/compact creation uses integer crops of the
same source density and content, with identical source/guide phase; an
unaligned compact crop is unavailable. Alignment cases include 31, 32,
63, 64 and 65 pixels. These are experiments, not provider requirements.
The current two-region-per-eye limit remains; four/eight-region experiments
await the capacity task.

All outputs are private. A raw NaN canary (or deterministic UNORM pattern)
is uploaded before each evaluation, outside the GPU timing scope. Readback
records unwritten/nonfinite pixels inside the requested rectangle and
writes outside it. A zero-edit or unwritten result cannot qualify as fast.
The batch timestamp includes barriers and cold creation; per-evaluation
timestamps exclude creation. Each iteration waits for completion and
reads back output, so these are isolated submission costs, not pipelined
steady-state game throughput. CPU elapsed time includes diagnostics.

Native auto-mask remains enabled. Occupancy cases use deterministic binary
1%, 25% and 100% masks only in a CPU composite after native inference, with
the same native rectangle, inputs and capacity. They choose either the
source pixel or private native output exactly once. This synthetic proxy
does not measure the CSX GPU compositor or its colour reconstruction; no
unverified provider `ControlMask` is supplied. Selected pixel counts and
composite hashes are retained separately from the unmasked native edit.
No NR-specific Streamline feature query is made without its plugin loaded.
The generic viewport limit does not establish native feature capacity.
Short GPU captures can supplement these results externally; absence of a
capture is recorded explicitly.

Run input-contract checks without loading the provider or claiming timings:

```powershell
python tools/nr-replay/test_input.py build/nr-replay/Release/csx_nr_replay.exe
```

The explicit `input-storage-captured`, `input-storage-zero` and
`input-storage-finite_pattern` cases use the same full backing capacity,
native rectangle and source/guide phase as `capacity-full`. They vary only
pixels outside the requested native colour/depth/motion rectangles. The
finite pattern uses ordinary finite values; it is not a NaN-input probe.
These cases require `--case` and are excluded from the default matrix.
Their purpose is to test whether the provider reads beyond the requested
rectangles, not to assume that those pixels are unused. Uploads remain fully
initialized. Per-input receipts retain full and valid-region hashes,
formats, rectangles and changed-pixel counts; valid pixels must remain exact.
`--validate-storage` checks these CPU transformations without loading NGX or
submitting GPU work. Native colour must be at least 256 by 256 pixels.

To vary preserved input context independently, select
`input-context-RESOURCE-POLICY-HALO`, where `RESOURCE` is `all`, `color`,
`depth` or `motion`, `POLICY` is `zero` or `finite_pattern`, and `HALO` is
0, 16, 32, 64, 128, 256, 512, 1024 or 16384. For example:

```powershell
./build/nr-replay/Release/csx_nr_replay.exe `
  --manifest '<capture>/manifest.json' --output build/nr-context-check-unique `
  --validate-storage --case input-context-color-finite_pattern-128
```

The halo expands the fixed output rectangle in native colour pixels,
clamps to backing capacity, then maps outward to each guide grid using the
production rectangle helpers. Only the selected resources change outside
that preserved context; all valid and preserved pixels must hash exactly.
The output rectangle, requested evaluation dimensions, texture capacity,
source phase and reset policy remain fixed. A 16384-pixel halo preserves
the complete supported backing image and provides a no-change control.
These cases also require explicit selection and are never in the default
GPU matrix. Without `--case`, `--validate-storage` retains its original
three-policy CPU check. Each receipt includes the selected resource, halo,
preserved rectangle/hash and effective per-resource policy.

Bracket context probes with captured-input references and reverse the probe
order. Equal native output establishes invariance only for the tested
input, rectangle, settings and history. It does not prove that the provider
never reads outside the preserved context, qualify compact creation, or
establish a universal halo for moving scenes. Compare timings separately:
identical output does not guarantee identical native cost.

Capacity and storage cases retain each requested native output crop as a
hashed binary resource. The reporter rejects comparisons with different
valid input content or an unplanned storage-policy change, and separately
reports exact output-crop hash equality when both sides retained evidence.
Different output is not a quality pass; bitwise equality covers the tested
native crops only, not perceptual, temporal or final CSX colour quality.
Paired timing and output-equality conclusions require complete matching
accepted sample sets. Partial case statistics remain descriptive only.
Earlier receipts without retained crops remain explicitly unavailable for
this comparison. Input hashing and output readback/writes are outside native
GPU timing; CPU elapsed time includes these diagnostics.

## Experimental region and transport qualification

The standalone executable enables the DevBench capacity ceiling: four
regions per eye is the target and eight is experimental. The game default
remains two. Explicit cases `duplicate-{1,2,4,8}-{private,shared}` create
separate native histories and outputs with identical 128x128 geometry.
`transport-private` and `transport-shared` use different rectangles over
one fixed source. Sharing retains one immutable colour/depth/motion input
per eye, deduplicates barriers with the renderer helper, and checks that
prepared colour is unchanged after native work.

`capacity-calls-{1,2,4,8}` holds 131,072 evaluated pixels per eye constant
while increasing calls; every rectangle is at least 128 pixels per side.
These cases are explicit-only. The earlier `calls-eight` 64x64 experiment
failed GPU completion and is unavailable. Do not substitute it or the
previously failed minimum-shape-31 probe into a normal campaign.

All count/transport cases preserve evaluated output crops with hashes and
per-call timing. Verify file count, size and hashes; an empty output list
cannot prove equality. Count both actual eyes and calls, preserve failures
and unavailable GPU timing, and distinguish native cost from in-game total
cost. This tool does not establish SE/AE gameplay, overlapping output
ownership, temporal quality or a production cost model.

## Independent-context and packed-region experiments

### Sequential native-handle reuse, preserving independent contexts

`native_handle_probe.py` is a separate offline-only experiment. It keeps
every original ROI, source texture, private output, pixel density, native
evaluation count and `reset=true` unchanged. It compares separate native
Feature-18 handles with one sequentially reused handle per eye. It does
not merge inputs or claim native batch support. Only stateless RGBA8 C
with equal input grids, two to four disjoint rectangles and both extents
at least 128 is admitted. Temporal, compact-capacity and storage-policy
combinations are rejected before provider admission.

```powershell
python tools/nr-replay/native_handle_probe.py `
  --manifest PATH/TO/manifest.json `
  --replay build/nr-replay/Release/csx_nr_replay.exe `
  --runtime PATH/TO/nvngx_dlssnr.dll `
  --output build/validation/NEW-HANDLE-PROBE `
  --roi 192,512,128,128 --roi 672,512,128,128 `
  --repeats 2 --warmup 3 --samples 8 --seconds 120
```

Coordinates are an example, not detected character ownership. Add
`--prepare-only` for immutable planning without native calls, or
`--alternate-output-sentinel` for the existing complementary marker.
The runner holds the existing campaign mutex, rejects a running game or
replay, verifies source/tool/executable/provider hashes and stops on the
first rejected sample. Each repeat uses fresh independent/reuse/independent
processes; the next repeat reverses that bracket. Existing evidence is never
overwritten. `--report PATH/TO/plan.json` rebuilds only the report.

The executable's explicit `--native-handle-policy independent|per-eye`
switch requires `--rects`. The same request with `--validate-input` checks
admission and writes `nativeHandlePlan` without loading NGX. This plan and
each recorded call distinguish resource/evaluation slots from actual native
handle slots. A conservative global UAV barrier precedes each reused-handle
call after the first; native batch time includes these barriers, while
per-evaluation GPU intervals are separate diagnostics. No extra CPU wait or
queue submission is inserted between calls.

The probe validates resident handle masks, cold/warm creation counts and
unchanged complete color/depth/motion inputs. Every evaluation retains its
private output crop and write-coverage evidence. All admitted samples must
match the independent baseline's RGBA bytes, including alpha, before a
qualified speed delta is reported. The surrounding batch-time baselines
must drift no more than 5% of their mean. Failed output or drift retains
descriptive timings and never qualifies production behavior. Internal
provider descriptor/scratch reuse remains unverified until this experiment
passes; `reset=true` alone is not proof of safe independent inference.

Run CPU report tests with `python -m unittest discover -s tools/nr-replay
-p test_native_handle_probe.py`; run the executable's expanded parser checks
with `python tools/nr-replay/test_input.py PATH/TO/csx_nr_replay.exe`.
No game renderer/default or production feature-handle policy changes.

The [2026-10-04 assessment](../../docs/development/nr-independent-context-batching-20261004.md)
records exact frozen-input RGBA equality for sequential per-eye reuse and
failed timing qualification. It does not establish a production speed gain.

### Packed-region inference

`packed_replay.py` is an offline feasibility experiment. It accepts two to
four disjoint owned rectangles in a frozen RGBA8 C capture with equal colour
and guide grids. Each eye has its own atlas. Inputs retain captured pixel
density, exact guide alignment, motion bytes and their explicit scale.
The pure input builder also tests rational guide grids; the native campaign
deliberately has the narrower admission contract above.

The campaign compares separate calls, one enclosing rectangle, and one
atlas call per eye for each requested context halo. Each halo also runs
with reversed tile order. Contexts may overlap in source space. Atlas
padding repeats the final context row within its own tile; only the original
owned pixels are compared after scattering. Full atlas pixels, including
context and padding, count toward evaluated area. All evaluations reset
history; this does not qualify temporal A/B or moving atlas layouts.

```powershell
python tools/nr-replay/packed_replay.py prepare `
  --manifest PATH/TO/manifest.json --output build/validation/NEW-CAMPAIGN `
  --replay build/nr-replay/Release/csx_nr_replay.exe `
  --roi 192,512,128,128 --roi 672,512,128,128 --halos 0,64,128 --repeats 3

python tools/nr-replay/packed_replay.py run `
  --campaign build/validation/NEW-CAMPAIGN/campaign.json `
  --runtime PATH/TO/nvngx_dlssnr.dll --warmup 3 --samples 8
```

Coordinates above are a fixture example, not automatic character detection.
Preparation validates inputs without loading the native provider. It writes
new derived bundles, preserves source metadata separately, and hashes the
source, executable and analysis code. Run refuses changed identities or
existing evidence. It holds an exclusive campaign mutex, rejects a running
Skyrim or replay process, reverses case order on alternate repeats, and
stops on failed native samples or bounded process timeout. Run with normal
graphics-driver IPC access; do not overlap with another GPU workload.

`summary.json` verifies every steady sample and retained crop, reports native
GPU timings, RGB errors, alpha differences, baseline repeatability and tile
order sensitivity. The strict gate is exact owned RGB equality; alpha is
reported separately. A completed measurement with differences is a quality
rejection, not a production pass. CPU packing, uploads, readback and scatter
are excluded from native timings. GPU preparation/composition and live
temporal/stereo quality require a later experiment if this gate passes.

The executable's `--rects '[[x,y,width,height],...]'` selects one explicit
stateless C case and retains output crops. Rectangles must be disjoint,
bounded and at least 64 pixels per side; more than two per eye requires
the shared 128-pixel experimental geometry floor. It cannot combine with
other case, storage, temporal or capacity switches.

### Native workload capture

`--renderdoc PATH/TO/renderdoc.dll` explicitly loads RenderDoc before device
creation and captures the first post-warmup sample. Use a current compatible
RenderDoc distribution; no capture library loads by default. Captured-run
timings are diagnostic and excluded from the uninstrumented comparisons.
`packed_replay.py capture` accepts the campaign/runtime arguments above plus
`--case separate` (or an exact atlas case ID) and `--renderdoc`. Every capture
has its own journal and directory; existing files are preserved.

Set `NR_REPLAY_CAPTURE` to the resulting `.rdc`, `NR_REPLAY_TRACE_OUTPUT` to
a new JSON path, and `NR_REPLAY_TRACE_SCRIPT` to the absolute path of
`renderdoc_workload.py`. Launch `qrenderdoc.exe --python` with that script
using a hidden window. Its embedded Python is required. Check the JSON
receipt's `complete`, state and gaps; qrenderdoc's exit code alone is not
an analysis pass. The audit retains dispatch dimensions, compute shader
identity, resources, API events and available instrumented GPU durations.
Missing vendor workloads, unsupported replay or counters remain explicit.
Dispatch counts cannot by themselves prove the provider's internal read
footprint, model batching support or uninstrumented GPU queue idle time.

The [October 4 qualification](../../docs/development/nr-packed-region-feasibility-20261004.md)
records `FAIL_PlatformError` during NGX initialization with RenderDoc 1.46
on this machine. No native capture was produced. Do not bypass NGX admission
or treat the optional capture path as qualified for that provider; the next
candidate is a compatible Nsight GPU Trace installation.

Offline regression commands:

```powershell
python tools/nr-replay/test_input.py build/nr-replay/Release/csx_nr_replay.exe
python -m unittest discover -s tools/nr-replay -p 'test_packed*.py'
```

These packed-input builders and offline runners add no renderer work,
game settings, shader permutation or production DLL dependency. The
separate qualified kernel-batching adapter is integrated into the game
DLL, as described below.

### Output-equivalence diagnosis

`context_probe.py` keeps the captured resources, backing extents, tuning,
motion scale and original coordinates fixed. It compares fresh full-image
and tight-region references with individual and shared enlarged valid
domains. Output ownership remains fixed. Each case uses fresh processes
and reset histories; alternate repeats reverse the case order.

```powershell
python tools/nr-replay/context_probe.py `
  --manifest PATH/TO/manifest.json --replay PATH/TO/csx_nr_replay.exe `
  --runtime PATH/TO/nvngx_dlssnr.dll --output build/validation/NEW-CONTEXT `
  --roi 192,512,128,128 --roi 672,512,128,128 `
  --halos 0,64,128,256 --samples 6 --warmup 3 --repeats 2
```

The report compares every retained owned crop against both fresh references
and checks their repeatability. Individual-context cost sums are explicitly
sums of independently measured means, not one batched measurement. The
64-pixel outward alignment comes from CSX's existing provider ROI policy;
it is not a verified internal network stride. `--prepare-only` creates the
plan without native calls; `--report PATH/TO/plan.json` regenerates analysis
while preserving execution provenance and missing-evidence reasons.

`translation_probe.py` rolls the complete captured grids horizontally and
moves one owned rectangle by the same amount. Texture extents, exact owned
bytes and motion scale are preserved. Its default shifts are 0, 1, 16, 32
and 64 pixels. It accepts the same manifest/replay/runtime/output arguments,
one `--roi`, and `--shifts 0,1,16,32,64`. Cyclic wrapping changes distant
boundary adjacency, so this is a translation/coordinate diagnostic rather
than a proof of unchanged global spatial context. It checks every retained
sample against the unshifted output and preserves rejected execution.

The packed campaign's `prepare --canvas horizontal|vertical|diagonal`
option creates a square control page from exactly two equal square contexts,
each duplicated twice without padding. The default remains `strip`. All
square arrangements have identical pixel populations and backing sizes.
Horizontal versus diagonal leaves both selected top-row contexts at the
same coordinates and rearranges only the duplicate bottom row. Source
indices, tile order and selected ownership remain explicit in the receipt.
Unequal clipped contexts or unequal guide grids are rejected.

These probes use the same bounded native runner, exclusive campaign mutex,
process exclusion, hashed input admission and retained-output verification.
They do not qualify perception, motion, stereo or production performance.
The [equivalence investigation](../../docs/development/nr-output-equivalence-investigation-20261004.md)
records the actual controls and distinguishes failed byte equality from
unmeasured perceptual equivalence.

```powershell
python -m unittest discover -s tools/nr-replay -p 'test_*probe.py'
python tools/nr-replay/test_canvas_input.py
```

### Experimental provider padding floor

`--experimental-provider-floor 256` is an explicit standalone experiment,
disabled by default. It changes one shared `MOV EAX,320` immediate to 256
inside the replay process. It never opens Skyrim, patches another process,
writes a provider DLL, or changes the production runtime. It preserves
captured full resource grids, original subrect coordinates, independent
handles, call count, reset policy and the provider's extra alignment branch.
The baseline is the same executable and arguments with this switch absent.

Admission requires provider disk SHA-256
`8270b350cd82de5ce89806872cdd6b6a9249b80836b91bbeb3573470744cc206`,
the pinned complete loaded code hash and exact instruction guard, and
committed read-only executable image pages belonging to that module. The
SHA-256 checks require exactly 64 hexadecimal digits and compare canonical
lowercase representations; letter case does not change provider identity.
The single patch is at RVA `0x3c83e`: `b8 40 01 00 00` becomes
`b8 00 01 00 00`. These constants come from the October 4 live-module
capture, not an assumption that another provider version behaves the same.
The request accepts one immutable stateless C frame, equal RGBA8 colour and
guide grids, and one to four disjoint custom regions with extents at least
128 pixels. Handle-reuse, capacity, temporal, storage, inspection and
RenderDoc controls cannot be combined with this experiment.

Build into a separate standalone directory using the dependency prefix
from your existing replay build, then run only the CPU validation first:

```powershell
pwsh ./tools/cmake.ps1 -S tools/nr-replay -B build/nr-floor-replay `
  -G 'Visual Studio 18 2026' -A x64 `
  -D 'CMAKE_PREFIX_PATH=<existing-vcpkg-installed>/x64-windows-static-md'
pwsh ./tools/cmake.ps1 --build build/nr-floor-replay --config Release
ctest --test-dir build/nr-floor-replay -C Release --output-on-failure
python tools/nr-replay/test_input.py build/nr-floor-replay/Release/csx_nr_replay.exe
```

After the existing exclusive GPU/process-exclusion checks, a matched pair
uses fresh output directories and the same frozen capture. Run an unpatched
reference before and after the candidate; retain all warmup and steady
samples, including spikes and failures. Example candidate command:

```powershell
build/nr-floor-replay/Release/csx_nr_replay.exe `
  --manifest PATH/TO/manifest.json --runtime PATH/TO/nvngx_dlssnr.dll `
  --output build/validation/NEW-FLOOR-CANDIDATE `
  --rects '[[448,576,192,256]]' --samples 16 --warmup 16 --seconds 240 `
  --experimental-provider-floor 256
```

The `providerFloorExperiment` receipt distinguishes unchanged disk identity
from experimental loaded code. A completed candidate must contain one
`applied_own_replay_process_only` transition followed by `restored`, an idle
proof, restored code hash and protection, and final
`inMemoryState: original_provider_code`. Application is repeated after each
runtime restart, before feature creation. Shutdown, including failure
unwinding, restores code only after GPU retirement. If retirement or restore
cannot be proven, the module is retained until process exit and the replay
fails with an explicit restoration record. No further native calls run
after a rejected sample.

Both reference and candidate retain outputs and validate untouched colour,
depth and motion, complete writes inside each ROI, finite results, and no
writes outside ownership for every warmup and measured sample. Compare
every retained owned output byte with the fresh references and check
reference repeatability before interpreting timing. The arithmetic model
predicts 192×256 active pixels change from 320×320 to 320×256, because the
extra width-alignment branch still applies. That is a padding model, not
measured dispatch geometry. These files explicitly leave visual quality and
production performance unqualified; floor reduction may change model
context or fail provider kernels and must not be promoted on timing alone.

### Native-layout qualification across A/B/C

`--qualify-native-layout` is an explicit replay admission path for original,
N1 and N2 comparisons over each route's captured native resources. It
requires `--rects` with one to four disjoint output rectangles per eye,
each at least 128 pixels on both axes. A kernel-pair run requires exactly
two regions in each of two eyes. Rectangles remain in native output
coordinates; backing resources, input pixel density, colour format and
motion-vector scale remain unchanged. Different region shapes are allowed.
The pair scheduler separately requires compatible original kernel entries,
parameter layouts and exact per-stage dispatch dimensions before recording
a paired launch. Admission alone is not a batching or output-equivalence pass.

Every captured eye must contain the exact full `nativeLayout`,
`callerReset` and `synchronizedHistoryReset` provenance. Replay checks it
against the resource descriptors and the production native-layout helper;
missing or inconsistent metadata is rejected. Complete finite tuning is
required, with style `0..3`. Supported colour/output formats are RGBA8,
R11G11B10_FLOAT, RGBA16_FLOAT and RGBA32_FLOAT; depth remains R32_FLOAT and
motion remains RG16_FLOAT. All resource files retain their recorded format
and tightly packed bytes. For example, the current A/B captures use
1512x1680 R11G11B10_FLOAT colour/output with 1008x1120 guides, while C uses
1008x1120 RGBA8 colour/output and guides. No format conversion, resampling,
padding change or inferred missing metadata is performed.

A complete capture may contain multiple consecutive frames. This path
selects frame index `0` and repeats that immutable source with a fresh
native context per region and `reset=true` on each evaluation. The result's
`nativeLayoutQualification` records the available frame count, selected
source frame/world frame, captured reset provenance, exact per-context
layouts, formats and tuning. `capturedHistoryReproduced` is false: earlier
in-game A/B histories are not reconstructed. Compare original, N1 and N2
under the same explicit `static_reset` policy; the capture's full native
output is an edit control, not an expected tight-region output.

The flag excludes native-handle reuse, provider-floor changes, capacity
crops, storage-policy changes, temporal controls and inspection mode. The
legacy C-only, equal-grid and fixed-shape guards remain when it is absent.
Use a fresh producer executable and unique output directory for every
lane. Preserve earlier measured executables, captures and receipts.

The following coordinates reproduce one captured C geometry, not a
universal character layout. Replace the manifest and rectangles with the
route and exact native regions being tested. No wrapper for the legacy
C-only campaign silently enables this path.

```powershell
$nativeArgs = @(
    '--manifest', '<capture>/manifest.json',
    '--runtime', '<physical-provider>/nvngx_dlssnr.dll',
    '--rects', '[[192,320,816,800],[448,64,192,256]]',
    '--qualify-native-layout',
    '--experimental-kernel-chain', 'forward',
    '--capture-kernel-modules', '--batch-timing-only',
    '--warmup', '3', '--samples', '4', '--seconds', '120'
)
$replay = '<fresh-producer>/csx_nr_replay.exe'
& $replay @nativeArgs --output build/validation/NEW-ORIGINAL `
    --experimental-kernel-pair original
& $replay @nativeArgs --output build/validation/NEW-N1 `
    --experimental-kernel-pair original `
    --experimental-model-replacement '<qualified-shared-n1-manifest.json>'
& $replay @nativeArgs --output build/validation/NEW-N2 `
    --experimental-kernel-pair model-batch `
    --experimental-model-replacement '<qualified-shared-n2-manifest.json>'
```

Run original first, then N1 before N2, and preserve a final original
reference. The pinned catalog must match the actual captured kernel
family; the flag does not waive module, entry, packet, descriptor-owner,
creation, barrier, heap, cache or retirement checks. Unknown or incompatible
native work remains rejected. These are instrumented native-provider gates,
not live A/B/C, temporal-quality or production-performance qualification.

The `csx-nr-replay-results-v1` result stores each raw evaluated output under
`cases[].samples[].outputFiles`. Each record contains `file`, `sha256`,
`format`, `width`, `height`, `rowBytes`, `slot` and
`scope: evaluated_rectangle`. Resolve files relative to that lane's output
directory; recheck byte length and SHA-256. Compare records by sample
iteration and physical slot with matching capture/source identity,
native layouts, tuning and reset policy. Compare all packed bytes in their
original DXGI format, including RGBA8 alpha where present; do not decode
HDR data into PNG or reinterpret it as RGBA8 for an exact-output gate.
For the four-region lanes above, require four private outputs per accepted
sample, input-integrity checks
and successful write footprints. Missing, failed, nonfinite, unwritten or
outside-owned output cannot establish equivalence.

With repeated schedules, full backing outputs additionally appear in
`kernelScheduleRepetitions[].outputs` with `scope: full_output_resource`,
`ownedRect` and `ownedSha256`. Compare the owned packed rectangle using its
recorded format and row stride; separately verify sentinel preservation
outside ownership. Keep submission CPU timing and native GPU timing
separate from hashing, readback and final CSX composition.

### Experimental native kernel-chain grouping

`--experimental-kernel-chain forward|group` is a standalone, default-off
experiment for the pinned provider used by the padding probe above. It
preserves the padding floor, full captured inputs, original coordinates,
private native handles, output ownership and reset policy. Admission accepts
one immutable C capture and one to four nonoverlapping, equal-shape regions
per eye, at least 128 pixels per side. The explicit native-layout path
above extends capture admission without changing the grouping algorithm.
Other provider, handle-reuse, capture, capacity and storage experiments
cannot be combined with it.

The experiment intercepts the provider's resolved `LaunchCuKernelChain`
function pointer in its own replay process. Forward mode retains the original
call boundaries. Group mode owns copies of parameter packets and coalesces
consecutive launches in their original order. A command-list proxy flushes
pending launches before every intervening GPU command, including barriers,
copies, state changes and timing queries. Unrecognized command-list interfaces
and interception failures stop the experiment before submission. No game
process, installed DLL, provider file or production render path is changed.

Compare an uninstrumented baseline, forwarding control, grouping candidate
and another baseline in fresh processes. Exact owned output bytes and all
existing input/footprint checks precede timing interpretation. The per-sample
`submissionCpuMicroseconds` covers the native evaluation loop and final flush,
excluding receipt encoding, GPU wait, downloads and file writes. Existing
`evalCpuMicroseconds` remains a narrower diagnostic. GPU batch timestamps
measure execution separately. Experimental per-evaluation timestamps enclose
feature creation on the first warmup only; steady samples must create no
features. Raw launch parameters, command boundaries and actual descriptor
group sizes remain in each experimental sample's receipt.

The maintained campaign passes `--batch-timing-only` to every case, including
both uninstrumented baselines. This omits per-region GPU timing commands so
instrumentation cannot itself prohibit cross-region grouping. Per-evaluation
GPU times are explicitly null; the GPU batch time remains measured. The
report requires the matching explicit plan, result and case flags and does
not relax ordinary replay timing admission.

```powershell
python tools/nr-replay/kernel_chain_probe.py `
  --manifest PATH/TO/manifest.json --replay PATH/TO/csx_nr_replay.exe `
  --runtime PATH/TO/nvngx_dlssnr.dll --output build/validation/NEW-CHAIN `
  --roi 256,576,192,256 --roi 640,576,192,256 `
  --samples 2 --warmup 3 --repeats 3
```

The [October 4 results](../../docs/development/nr-independent-context-batching-20261004.md)
preserve the exact-output pass, reduced driver calls, GPU regression and
separate longer-window timing limitations. `--prepare-only` creates the
immutable plan without GPU execution; `--report PATH/TO/plan.json` audits
retained evidence without rerunning native work.

Grouping within one region and grouping across regions are separate results.
Zero multi-descriptor submissions is not a batching pass. This experiment
does not remove barriers, reorder independent regions, implement a tensor
batch, or establish proportional GPU cost from API-call counts alone.

`--capture-kernel-modules` additionally records live module creation and
function binding in the forwarding probe. It saves bounded copies of the
actual module blobs under the new replay output's `kernel-modules` directory,
with hashes, entry names, returned handles and launch correlations. The
option requires `--experimental-kernel-chain forward`; it is off by default
and cannot be combined with grouping. Unknown launch identities stop the
capture. This instrument identifies the programs that a future kernel
replacement must preserve; it does not provide batched kernels or qualify
performance. Capture runs perform file I/O during feature creation and
must be treated as instrumentation runs.

`--experimental-kernel-replacement MANIFEST` is a separate single-region
equivalence gate requiring live forwarding and module capture. It admits
only the pinned SM120 `inpview_tilesync_fp8` replacement, original module
identity and 96-byte parameter packet. The manifest identifies the exact
candidate path, SHA-256 and entry under `nr-n1-kernel-replacement-v1` with
`batchCount: 1`. Original provider handles remain unchanged; only matching
recorded launches use the privately owned replacement function. Each
measured sample must exercise it. Private objects retire after GPU idle and
cache restoration. Parser-only mode cannot qualify this manifest.

This gate tests whether rebuilt metadata and unchanged instructions preserve
native output before any multi-region dispatch. It does not enable NR
batching, and its instrumented timings cannot qualify a performance result.

### Experimental independent first-stage batching

`--experimental-kernel-pair original|control|layer-control|batch` qualifies the first active
tilesync stage with two separate region contexts per eye. It requires the
forward probe, module capture, batch-only timing and at least one unchanged
warmup. Legacy admission is limited to the pinned SM120 provider,
stateless C, two 192x256 regions per eye and the captured 1008x1120 input
grid. `--qualify-native-layout` enables the separate exact-layout path
above; compatible pairs may span eyes when region shapes differ. Native
handles, full input context, coordinates and reset policy remain separate.

Steady evaluations record owned launch packets, barriers and heap bindings
before emitting GPU commands. The `original` control emits the recorded
region order. The `control` lane prepares both regions, runs their original
first stages, inserts one global UAV barrier and resumes their unchanged
suffixes. The `batch` lane uses the same schedule and replaces the two first
stages with one N=2 launch. All later stages remain separate. This is a
selected-stage batching experiment, not complete-model batching.

The `layer-control` lane runs the two original kernels at every corresponding
stage before advancing to the next stage, with a global UAV join after each
pair. It preserves every original barrier and heap command in per-region
order. All 632 original kernels still execute; this isolates the full-model
scheduling change from kernel replacement and is not a performance result.

Use `--experimental-kernel-replacement MANIFEST` only with the `batch` pair
lane. Its schema is `nr-pair-kernel-replacement-v1`, `batchCount` is 2,
`original.paramSize` stays 96, and the pinned candidate consumes the two
unchanged packets in 192 bytes. The original/control lanes have no replacement
manifest. Existing N=1 manifests remain confined to the N=1 gate.

Warmup records bind each region's tensor and completion addresses to retained
scratch and weight buffers. A standalone generated-source observer reads the
exact NGX creation parameters to require absent/null allocation callbacks;
it does not change production source or the parameters. Provider cache-miss
observation must establish stable descriptors throughout deferred recording.
Unexpected creation, descriptor mutation, commands, packet dependencies or
resource ownership abort the unsubmitted command list. Private resources and
owned packets remain retained through proven GPU idle and hook restoration.

Run a fresh uninstrumented baseline, forwarding capture, recorded original
control, reordered control and N=2 candidate in that order. Compare every
saved output, input-integrity check and write footprint before interpreting
the candidate. Preserve the actual physical launch and its packed bytes,
separately from its two logical source descriptors. These instrumented
qualification receipts are excluded from production timing reports.

`--experimental-model-replacement MANIFEST` extends the N=1 execution gate
to the entire captured model using the `original` or `layer-control` pair
schedule. It pins the semantic identity of nine candidate modules and all
44 launched entries, including each original packet size and grid-Z extent.
Candidate paths can move, but their contents and entry contracts cannot.
The qualified SM120 kernel family and complete four-region ABI remain
required; explicit native-layout qualification preserves other captured
grids and region shapes while checking their actual dispatches. Every sample must
exercise all entries; each submitted descriptor records the original and
private function handles. Private modules, functions, parameter storage and
device references remain owned through proven GPU idle and cache restoration.
Uncertain retirement retains ownership until process exit. This offline
control does not change the installed in-game adapter or qualify performance.

The `model-batch` pair lane admits a separately pinned N=2 catalog after the
corresponding transformed N=1 control has passed. Warmups prepare all private
functions while forwarding the original kernels. Steady samples prepare the
entire 316-dispatch plan before recording any GPU commands. Each launch keeps
the original X/Y grid, block and shared-memory size and doubles grid Z. The
candidate selects one region and restores its original logical Z coordinate.
Its parameter packet contains both unchanged original packets, each padded
with zeros to a 16-byte stride. Every paired stage uses separate original
context pointers, including scratch, history, output and completion state.
The conservative schedule retains original commands and adds a global UAV
join after each pair. It represents 632 logical stages with 316 physical
kernel launches; actual execution, output equivalence and cost require their
own receipts. The qualified batching family is also used by the in-game
adapter; replay commands do not modify or install the game DLL.

For N1 fault isolation, `--experimental-model-n1-stages N` replaces only
the first `N` stages in each region, including warmup (`1..158`, default
`158`). Later stages retain their original handles and packets. Partial
N1 runs require a single-pass original or layer-control schedule and still
prepare all 44 private functions. Their receipts verify all 158 original
stages per region and exact per-function private submission counts. This
diagnostic cutoff is unavailable to the in-game adapter.

For fault isolation, `--experimental-model-batch-stages N` limits N=2
dispatches to the first 1..158 stages in each compatible pair. Later stages follow the
unchanged `layer-control` schedule. The receipt records the limit and exact
per-entry submission counts, including zero for unselected replacements.
The full image and ownership checks remain required; a prefix pass does not
qualify the complete model.

`--experimental-kernel-repetitions 2|3|4` is a separate sustained-GPU
diagnostic for the complete `original`, `layer-control` or `model-batch`
schedule. It retains one admitted set of static-reset packets and records
the entire schedule repeatedly in one command list. Unchanged warmups
still execute once. A global UAV boundary separates steady repetitions;
there is no intermediate CPU wait or readback. The four-repetition limit
bounds submitted work; the existing idle timeout still applies.

Each repetition resets all outputs to alternating sentinels, timestamps
only its complete kernel schedule, and copies every full output into an
independent readback buffer. Reset, copy and boundary work remain outside
the per-repetition GPU intervals. After final GPU idle, the replay saves
every full output, verifies each write footprint and requires identical
owned pixels across repetitions. Existing per-region GPU timings remain
null because their query slots hold explicitly labeled schedule timings.
The enclosing batch time includes the reset and copy work. CPU submission
timing covers one provider recording plus all repeated submissions and
copy commands; it is not normal per-frame CPU cost. Failed idle proof
retains all diagnostic resource owners until process exit. This mode is
offline-only and changes neither the production DLL nor its timeout.

`--experimental-kernel-comparison original|layer-control` requires the
complete `model-batch` schedule and four repetitions. It interleaves two
control executions with two N2 executions using the same captured packets
in one command list. Even iteration numbers use control/batch/batch/control;
odd iterations use batch/control/control/batch, including the warmup offset
in the iteration number. Warmups remain unchanged single executions. The
receipt identifies each repetition's mode and each actual submitted native
function, grid, block, parameter packet and status. All four outputs and
sentinel footprints remain mandatory. These paired timings diagnose GPU
scheduling variation; they do not measure production frame cost.

The [batching assessment](../../docs/development/nr-independent-context-batching-20261004.md)
records the exact-output gates and matched timings. The qualified kernel
family is integrated into the game DLL and its AIO kernel catalog, with
selectable automatic single ROI, independent multi-ROI and batched
multi-ROI methods. Unsupported native graphs use the independent path
with a visible reason. This offline qualifier does not extend in-game
admission or establish live A/B/C output quality or performance.

`preserved_elf.py` emits native CUDA ELF from admitted live captures while
preserving resource, stack, symbol and relocation metadata. It consumes
explicit instruction/target maps and rejects unsupported metadata. It never
discovers installed binaries or runs GPU work. Validate its synthetic fixtures
with `python tools/nr-replay/test_preserved_elf.py`; candidate qualification
also requires source hashes, official disassembly/resource checks and exact
runtime output gates.

### Reproduce the captured N1/N2 model catalogs

`clone_model.py` combines the native instruction transformation, scheduled
selector, logical-Z dependency correction and original ELF metadata emission.
It regenerates all 44 entries from the pinned live-capture inventory; it does
not consume an earlier candidate or resume from partially generated entries.
The source bodies retain their arithmetic and private context pointers.
Immediate masks after S2R/S2UR use a producer delay of at least two cycles,
matching the qualified dependency correction. The selector fixture's sleep
bodies exist only to obtain compiler-generated branch instructions; those
bodies are never included in the model kernels.

The command requires the retained capture tree and explicit CPU tool paths:

```powershell
$cloneArgs = @(
    'tools/nr-replay/clone_model.py',
    '--inventory', '<capture-root>/full-chain-static-01/inventory.json',
    '--capture-root', '<capture-root>',
    '--output', '<new-empty-output-directory>',
    '--cubit', '<pinned-cubit.exe>',
    '--table', '<pinned-sm120.json>',
    '--nvdisasm', '<pinned-nvdisasm.exe>',
    '--cuobjdump', '<pinned-cuobjdump.exe>',
    '--ptxas', '<pinned-ptxas.exe>'
)
python @cloneArgs
python tools/nr-replay/test_clone_model.py
```

The output directory must not exist. Captured paths are relocated beneath
`--capture-root`; source results, module blobs, extracted cubins, inventory
and tools must match their recorded SHA-256 identities. Tool pins in the
module identify Cubit source revision
`1f7a5aa6cb0096223f4054930b58c9c0208251e5`, its SM120 table, NVIDIA PTXAS
13.4.59, and nvdisasm/cuobjdump 13.4.49. The qualified Cubit executable is
also pinned because replacing its build requires renewed reproduction.
No compiler, assembler, provider or generated binary is versioned here.

Every subprocess has a 120-second bound and retained output. Failure and
interruption leave an unsuccessful `audit.json`; prior output is never
overwritten. Successful generation requires official disassembly and
resource checks and exact catalog reproduction:

-   N1: `a8d6d4ebddadfa2ddfef5fa960a7a0713403ceceb7324fde3045eb52871bf281`
-   N2: `2b11eb5028bbdbb3b539be8789f10052868f46b5c256bdc16c1003a3e7f8d76e`

The final manifests are `full-model-n1-manifest.json` and
`full-model-n2-manifest.json`. Files marked `UNQUALIFIED-METADATA` are
intermediate assembler output and must never execute. This command performs
no GPU calls or game changes; replay qualification and performance evidence
remain separate from CPU reproduction. An inventory or toolchain change
requires a reviewed pin update and renewed validation.
