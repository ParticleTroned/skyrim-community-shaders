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
Use normal local driver IPC access for GPU replay: the October 1 sandboxed
SDK-only check stalled in NVIDIA telemetry shutdown, while the identical
check and replay exited normally outside the sandbox. Preserve such cleanup
diagnostics separately from completed GPU samples.

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

## One inference per eye over packed regions

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

All these additions belong to the standalone tool. They add no renderer
work, game settings, shader permutation or production DLL dependency.
